/**
 * @file video_decoder.cpp
 * @brief Implementation of video decoder using FFmpeg
 */

#include "video_decoder.h"
#include <tinyvk/core/log.h>

namespace tvk_media {

VideoDecoder::VideoDecoder()
    : _formatContext(nullptr)
    , _codecContext(nullptr)
    , _videoStream(nullptr)
    , _swsContext(nullptr)
    , _frame(nullptr)
    , _packet(nullptr)
    , _videoStreamIndex(-1)
    , _width(0)
    , _height(0)
    , _fps(0.0)
    , _duration(0.0)
    , _currentTime(0.0)
{
}

VideoDecoder::~VideoDecoder() {
    Close();
}

bool VideoDecoder::Open(const std::string& filepath) {
    Close();

    // Open the video file
    if (avformat_open_input(&_formatContext, filepath.c_str(), nullptr, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to open video file: {}", filepath);
        return false;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(_formatContext, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to find stream information");
        Close();
        return false;
    }

    // Find the video stream
    _videoStreamIndex = -1;
    for (unsigned int i = 0; i < _formatContext->nb_streams; i++) {
        if (_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            _videoStreamIndex = i;
            _videoStream = _formatContext->streams[i];
            break;
        }
    }

    if (_videoStreamIndex == -1) {
        TVK_LOG_ERROR("No video stream found");
        Close();
        return false;
    }

    // Get codec parameters
    AVCodecParameters* codecParams = _videoStream->codecpar;
    
    // Find the decoder for the video stream
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        TVK_LOG_ERROR("Unsupported codec");
        Close();
        return false;
    }

    // Allocate codec context
    _codecContext = avcodec_alloc_context3(codec);
    if (!_codecContext) {
        TVK_LOG_ERROR("Failed to allocate codec context");
        Close();
        return false;
    }

    // Copy codec parameters to context
    if (avcodec_parameters_to_context(_codecContext, codecParams) < 0) {
        TVK_LOG_ERROR("Failed to copy codec parameters");
        Close();
        return false;
    }

    // Open codec
    if (avcodec_open2(_codecContext, codec, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to open codec");
        Close();
        return false;
    }

    // Store video information
    _width = _codecContext->width;
    _height = _codecContext->height;
    
    // Calculate FPS
    if (_videoStream->avg_frame_rate.den != 0) {
        _fps = av_q2d(_videoStream->avg_frame_rate);
    } else {
        _fps = 30.0; // Default fallback
    }

    // Calculate duration
    if (_formatContext->duration != AV_NOPTS_VALUE) {
        _duration = (double)_formatContext->duration / AV_TIME_BASE;
    } else if (_videoStream->duration != AV_NOPTS_VALUE) {
        _duration = (double)_videoStream->duration * av_q2d(_videoStream->time_base);
    }

    // Allocate frame and packet
    _frame = av_frame_alloc();
    _packet = av_packet_alloc();

    if (!_frame || !_packet) {
        TVK_LOG_ERROR("Failed to allocate frame or packet");
        Close();
        return false;
    }

    // Initialize software scaler for RGBA conversion
    _swsContext = sws_getContext(
        _width, _height, _codecContext->pix_fmt,
        _width, _height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!_swsContext) {
        TVK_LOG_ERROR("Failed to initialize sws context");
        Close();
        return false;
    }

    _currentTime = 0.0;

    TVK_LOG_INFO("Video opened successfully:");
    TVK_LOG_INFO("  Resolution: {}x{}", _width, _height);
    TVK_LOG_INFO("  FPS: {:.2f}", _fps);
    TVK_LOG_INFO("  Duration: {:.2f} seconds", _duration);

    return true;
}

void VideoDecoder::Close() {
    Cleanup();
}

bool VideoDecoder::DecodeNextFrame(VideoFrame& outFrame) {
    if (!_formatContext || !_codecContext) {
        return false;
    }

    while (true) {
        // Read packets until we get a video frame
        int ret = av_read_frame(_formatContext, _packet);
        
        if (ret < 0) {
            // End of file or error
            return false;
        }

        // Check if packet belongs to video stream
        if (_packet->stream_index != _videoStreamIndex) {
            av_packet_unref(_packet);
            continue;
        }

        // Send packet to decoder
        ret = avcodec_send_packet(_codecContext, _packet);
        av_packet_unref(_packet);

        if (ret < 0) {
            TVK_LOG_ERROR("Error sending packet to decoder");
            return false;
        }

        // Receive frame from decoder
        ret = avcodec_receive_frame(_codecContext, _frame);

        if (ret == AVERROR(EAGAIN)) {
            // Need more packets
            continue;
        } else if (ret < 0) {
            TVK_LOG_ERROR("Error receiving frame from decoder");
            return false;
        }

        // We have a frame - convert it to RGBA
        outFrame.width = _width;
        outFrame.height = _height;
        outFrame.data.resize(_width * _height * 4); // RGBA format

        // Calculate timestamp
        if (_frame->pts != AV_NOPTS_VALUE) {
            outFrame.timestamp = _frame->pts * av_q2d(_videoStream->time_base);
            _currentTime = outFrame.timestamp;
        } else {
            outFrame.timestamp = _currentTime;
        }

        // Setup destination buffer
        uint8_t* dest[4] = { outFrame.data.data(), nullptr, nullptr, nullptr };
        int destLinesize[4] = { _width * 4, 0, 0, 0 };

        // Convert to RGBA
        sws_scale(
            _swsContext,
            _frame->data, _frame->linesize,
            0, _height,
            dest, destLinesize
        );

        av_frame_unref(_frame);
        return true;
    }
}

bool VideoDecoder::Seek(double timeSeconds) {
    if (!_formatContext || !_videoStream) {
        return false;
    }

    int64_t timestamp = (int64_t)(timeSeconds / av_q2d(_videoStream->time_base));

    if (av_seek_frame(_formatContext, _videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        TVK_LOG_ERROR("Failed to seek to {:.2f} seconds", timeSeconds);
        return false;
    }

    avcodec_flush_buffers(_codecContext);
    _currentTime = timeSeconds;

    return true;
}

bool VideoDecoder::GetThumbnailAt(double timeSeconds, VideoFrame& outFrame, int maxWidth, int maxHeight) {
    if (!_formatContext || !_videoStream || !_codecContext) {
        return false;
    }

    int64_t timestamp = (int64_t)(timeSeconds / av_q2d(_videoStream->time_base));
    if (av_seek_frame(_formatContext, _videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(_codecContext);

    AVFrame* tempFrame = av_frame_alloc();
    AVPacket* tempPacket = av_packet_alloc();
    if (!tempFrame || !tempPacket) {
        if (tempFrame) av_frame_free(&tempFrame);
        if (tempPacket) av_packet_free(&tempPacket);
        return false;
    }

    bool gotFrame = false;
    int attempts = 0;
    while (attempts < 50 && !gotFrame) {
        int ret = av_read_frame(_formatContext, tempPacket);
        if (ret < 0) break;

        if (tempPacket->stream_index == _videoStreamIndex) {
            ret = avcodec_send_packet(_codecContext, tempPacket);
            if (ret >= 0) {
                ret = avcodec_receive_frame(_codecContext, tempFrame);
                if (ret >= 0) {
                    gotFrame = true;
                }
            }
        }
        av_packet_unref(tempPacket);
        attempts++;
    }

    if (!gotFrame) {
        av_frame_free(&tempFrame);
        av_packet_free(&tempPacket);
        return false;
    }

    float aspect = (float)_width / (float)_height;
    int thumbW = maxWidth;
    int thumbH = (int)(maxWidth / aspect);
    if (thumbH > maxHeight) {
        thumbH = maxHeight;
        thumbW = (int)(maxHeight * aspect);
    }

    SwsContext* thumbSws = sws_getContext(
        _width, _height, _codecContext->pix_fmt,
        thumbW, thumbH, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!thumbSws) {
        av_frame_free(&tempFrame);
        av_packet_free(&tempPacket);
        return false;
    }

    outFrame.width = thumbW;
    outFrame.height = thumbH;
    outFrame.data.resize(thumbW * thumbH * 4);
    outFrame.timestamp = timeSeconds;

    uint8_t* dest[4] = { outFrame.data.data(), nullptr, nullptr, nullptr };
    int destLinesize[4] = { thumbW * 4, 0, 0, 0 };

    sws_scale(thumbSws, tempFrame->data, tempFrame->linesize, 0, _height, dest, destLinesize);

    sws_freeContext(thumbSws);
    av_frame_free(&tempFrame);
    av_packet_free(&tempPacket);

    return true;
}

void VideoDecoder::Cleanup() {
    if (_swsContext) {
        sws_freeContext(_swsContext);
        _swsContext = nullptr;
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

    _videoStream = nullptr;
    _videoStreamIndex = -1;
    _width = 0;
    _height = 0;
    _fps = 0.0;
    _duration = 0.0;
    _currentTime = 0.0;
}

} // namespace tvk_media
