/**
 * @file audio_decoder.h
 * @brief Audio decoder and player using FFmpeg and OpenAL
 */

#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <AL/al.h>
#include <AL/alc.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

namespace tvk_media {

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    bool Open(const std::string& filepath);
    void Close();
    
    void Play();
    void Pause();
    void Stop();
    bool Seek(double timeSeconds);
    
    void Update();
    
    void SetVolume(float volume);
    float GetVolume() const { return _volume; }
    
    double GetCurrentTime() const { return _currentTime; }
    double GetDuration() const { return _duration; }
    int GetSampleRate() const { return _sampleRate; }
    int GetChannels() const { return _channels; }
    bool IsPlaying() const { return _isPlaying; }
    bool HasAudio() const { return _hasAudio; }
    std::vector<int> GetAvailableAudioStreamIndices() const;
    std::vector<std::string> GetAvailableAudioStreamNames() const;
    int GetSelectedAudioStreamIndex() const;
    bool SelectAudioStream(int stream_index, double sync_time);

private:
    static constexpr int NUM_BUFFERS = 4;
    static constexpr int BUFFER_SIZE = 65536;

    bool InitOpenAL();
    void CleanupOpenAL();
    bool DecodeAudioPacket(std::vector<uint8_t>& outData);
    bool FillBuffer(ALuint buffer);
    void QueueBuffers();

    AVFormatContext* _formatContext;
    AVCodecContext* _codecContext;
    AVStream* _audioStream;
    SwrContext* _swrContext;
    AVFrame* _frame;
    AVPacket* _packet;

    int _audioStreamIndex;
    std::vector<int> _availableAudioStreamIndices;
    std::vector<std::string> _availableAudioStreamNames;
    int _sampleRate;
    int _channels;
    double _duration;
    std::atomic<double> _currentTime;
    float _volume;

    ALCdevice* _alDevice;
    ALCcontext* _alContext;
    ALuint _alSource;
    ALuint _alBuffers[NUM_BUFFERS];
    
    std::atomic<bool> _isPlaying;
    std::atomic<bool> _hasAudio;
    std::mutex _decodeMutex;

    void Cleanup();
};

} // namespace tvk_media
