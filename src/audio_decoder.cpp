#include "audio_decoder.h"
#include <tinyvk/core/log.h>
#include <cstring>

namespace tvk_media {

AudioDecoder::AudioDecoder()
    : _formatContext(nullptr)
    , _codecContext(nullptr)
    , _audioStream(nullptr)
    , _swrContext(nullptr)
    , _frame(nullptr)
    , _packet(nullptr)
    , _audioStreamIndex(-1)
    , _sampleRate(0)
    , _channels(0)
    , _duration(0.0)
    , _currentTime(0.0)
    , _volume(1.0f)
    , _alDevice(nullptr)
    , _alContext(nullptr)
    , _alSource(0)
    , _isPlaying(false)
    , _hasAudio(false)
{
    for (int i = 0; i < NUM_BUFFERS; i++) {
        _alBuffers[i] = 0;
    }
}

AudioDecoder::~AudioDecoder() {
    Close();
}

bool AudioDecoder::InitOpenAL() {
    _alDevice = alcOpenDevice(nullptr);
    if (!_alDevice) {
        TVK_LOG_ERROR("Failed to open OpenAL device");
        return false;
    }

    _alContext = alcCreateContext(_alDevice, nullptr);
    if (!_alContext) {
        TVK_LOG_ERROR("Failed to create OpenAL context");
        alcCloseDevice(_alDevice);
        _alDevice = nullptr;
        return false;
    }

    alcMakeContextCurrent(_alContext);

    alGenSources(1, &_alSource);
    alGenBuffers(NUM_BUFFERS, _alBuffers);

    alSourcef(_alSource, AL_GAIN, _volume);
    alSourcei(_alSource, AL_LOOPING, AL_FALSE);

    return true;
}

void AudioDecoder::CleanupOpenAL() {
    if (_alSource) {
        alSourceStop(_alSource);
        alSourcei(_alSource, AL_BUFFER, 0);
        alDeleteSources(1, &_alSource);
        _alSource = 0;
    }

    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (_alBuffers[i]) {
            alDeleteBuffers(1, &_alBuffers[i]);
            _alBuffers[i] = 0;
        }
    }

    if (_alContext) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(_alContext);
        _alContext = nullptr;
    }

    if (_alDevice) {
        alcCloseDevice(_alDevice);
        _alDevice = nullptr;
    }
}

bool AudioDecoder::Open(const std::string& filepath) {
    Close();

    if (!InitOpenAL()) {
        return false;
    }

    if (avformat_open_input(&_formatContext, filepath.c_str(), nullptr, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to open audio file: {}", filepath);
        CleanupOpenAL();
        return false;
    }

    if (avformat_find_stream_info(_formatContext, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to find stream information");
        Close();
        return false;
    }

    _audioStreamIndex = -1;
    for (unsigned int i = 0; i < _formatContext->nb_streams; i++) {
        if (_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            _audioStreamIndex = i;
            _audioStream = _formatContext->streams[i];
            break;
        }
    }

    if (_audioStreamIndex == -1) {
        TVK_LOG_INFO("No audio stream found");
        Close();
        return false;
    }

    AVCodecParameters* codecParams = _audioStream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        TVK_LOG_ERROR("Unsupported audio codec");
        Close();
        return false;
    }

    _codecContext = avcodec_alloc_context3(codec);
    if (!_codecContext) {
        TVK_LOG_ERROR("Failed to allocate audio codec context");
        Close();
        return false;
    }

    if (avcodec_parameters_to_context(_codecContext, codecParams) < 0) {
        TVK_LOG_ERROR("Failed to copy audio codec parameters");
        Close();
        return false;
    }

    if (avcodec_open2(_codecContext, codec, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to open audio codec");
        Close();
        return false;
    }

    _sampleRate = _codecContext->sample_rate;
    _channels = _codecContext->ch_layout.nb_channels;

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, _channels > 2 ? 2 : _channels);
    
    int ret = swr_alloc_set_opts2(&_swrContext,
        &outLayout, AV_SAMPLE_FMT_S16, _sampleRate,
        &_codecContext->ch_layout, _codecContext->sample_fmt, _codecContext->sample_rate,
        0, nullptr);
    
    if (ret < 0 || !_swrContext || swr_init(_swrContext) < 0) {
        TVK_LOG_ERROR("Failed to initialize audio resampler");
        Close();
        return false;
    }

    _channels = outLayout.nb_channels;

    if (_formatContext->duration != AV_NOPTS_VALUE) {
        _duration = (double)_formatContext->duration / AV_TIME_BASE;
    } else if (_audioStream->duration != AV_NOPTS_VALUE) {
        _duration = (double)_audioStream->duration * av_q2d(_audioStream->time_base);
    }

    _frame = av_frame_alloc();
    _packet = av_packet_alloc();

    if (!_frame || !_packet) {
        TVK_LOG_ERROR("Failed to allocate audio frame or packet");
        Close();
        return false;
    }

    _currentTime = 0.0;
    _hasAudio = true;

    QueueBuffers();

    TVK_LOG_INFO("Audio opened successfully:");
    TVK_LOG_INFO("  Sample Rate: {} Hz", _sampleRate);
    TVK_LOG_INFO("  Channels: {}", _channels);
    TVK_LOG_INFO("  Duration: {} seconds", _duration);

    return true;
}

void AudioDecoder::Close() {
    Stop();
    Cleanup();
}

bool AudioDecoder::DecodeAudioPacket(std::vector<uint8_t>& outData) {
    while (true) {
        int ret = av_read_frame(_formatContext, _packet);
        if (ret < 0) {
            return false;
        }

        if (_packet->stream_index != _audioStreamIndex) {
            av_packet_unref(_packet);
            continue;
        }

        ret = avcodec_send_packet(_codecContext, _packet);
        av_packet_unref(_packet);

        if (ret < 0) {
            return false;
        }

        ret = avcodec_receive_frame(_codecContext, _frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            return false;
        }

        int outSamples = swr_get_out_samples(_swrContext, _frame->nb_samples);
        int outSize = outSamples * _channels * sizeof(int16_t);
        outData.resize(outSize);

        uint8_t* outPtr = outData.data();
        int converted = swr_convert(_swrContext, &outPtr, outSamples,
                                    (const uint8_t**)_frame->data, _frame->nb_samples);

        if (converted < 0) {
            av_frame_unref(_frame);
            return false;
        }

        outData.resize(converted * _channels * sizeof(int16_t));

        if (_frame->pts != AV_NOPTS_VALUE) {
            _currentTime = _frame->pts * av_q2d(_audioStream->time_base);
        }

        av_frame_unref(_frame);
        return true;
    }
}

bool AudioDecoder::FillBuffer(ALuint buffer) {
    std::vector<uint8_t> audioData;
    size_t totalSize = 0;
    std::vector<uint8_t> combinedData;
    combinedData.reserve(BUFFER_SIZE);

    while (totalSize < BUFFER_SIZE) {
        if (!DecodeAudioPacket(audioData)) {
            break;
        }
        combinedData.insert(combinedData.end(), audioData.begin(), audioData.end());
        totalSize += audioData.size();
    }

    if (combinedData.empty()) {
        return false;
    }

    ALenum format = (_channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alBufferData(buffer, format, combinedData.data(), (ALsizei)combinedData.size(), _sampleRate);

    return true;
}

void AudioDecoder::QueueBuffers() {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (!FillBuffer(_alBuffers[i])) {
            break;
        }
        alSourceQueueBuffers(_alSource, 1, &_alBuffers[i]);
    }
}

void AudioDecoder::Play() {
    if (!_hasAudio) return;
    
    alSourcePlay(_alSource);
    _isPlaying = true;
}

void AudioDecoder::Pause() {
    if (!_hasAudio) return;
    
    alSourcePause(_alSource);
    _isPlaying = false;
}

void AudioDecoder::Stop() {
    if (!_hasAudio) return;
    
    alSourceStop(_alSource);
    _isPlaying = false;
}

bool AudioDecoder::Seek(double timeSeconds) {
    std::lock_guard<std::mutex> lock(_decodeMutex);
    
    if (!_formatContext || !_audioStream) {
        return false;
    }

    alSourceStop(_alSource);
    
    ALint queued;
    alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint buffer;
        alSourceUnqueueBuffers(_alSource, 1, &buffer);
    }

    int64_t timestamp = (int64_t)(timeSeconds / av_q2d(_audioStream->time_base));
    if (av_seek_frame(_formatContext, _audioStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }

    avcodec_flush_buffers(_codecContext);
    _currentTime = timeSeconds;

    QueueBuffers();

    if (_isPlaying) {
        alSourcePlay(_alSource);
    }

    return true;
}

void AudioDecoder::Update() {
    if (!_hasAudio || !_isPlaying) return;
    
    std::lock_guard<std::mutex> lock(_decodeMutex);

    ALint state;
    alGetSourcei(_alSource, AL_SOURCE_STATE, &state);

    ALint processed;
    alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);

    while (processed-- > 0) {
        ALuint buffer;
        alSourceUnqueueBuffers(_alSource, 1, &buffer);

        if (FillBuffer(buffer)) {
            alSourceQueueBuffers(_alSource, 1, &buffer);
        }
    }

    if (state != AL_PLAYING && _isPlaying) {
        ALint queued;
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0) {
            alSourcePlay(_alSource);
        } else {
            _isPlaying = false;
        }
    }
}

void AudioDecoder::SetVolume(float volume) {
    _volume = volume;
    if (_volume < 0.0f) _volume = 0.0f;
    if (_volume > 1.0f) _volume = 1.0f;
    
    if (_alSource) {
        alSourcef(_alSource, AL_GAIN, _volume);
    }
}

void AudioDecoder::Cleanup() {
    CleanupOpenAL();

    if (_swrContext) {
        swr_free(&_swrContext);
        _swrContext = nullptr;
    }

    if (_frame) {
        av_frame_free(&_frame);
        _frame = nullptr;
    }

    if (_packet) {
        av_packet_free(&_packet);
        _packet = nullptr;
    }

    if (_codecContext) {
        avcodec_free_context(&_codecContext);
        _codecContext = nullptr;
    }

    if (_formatContext) {
        avformat_close_input(&_formatContext);
        _formatContext = nullptr;
    }

    _audioStream = nullptr;
    _audioStreamIndex = -1;
    _sampleRate = 0;
    _channels = 0;
    _duration = 0.0;
    _currentTime = 0.0;
    _hasAudio = false;
    _isPlaying = false;
}

} // namespace tvk_media
