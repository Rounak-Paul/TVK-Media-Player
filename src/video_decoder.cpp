#include "video_decoder.h"
#include <tinyvk/core/log.h>

namespace tvk_media {

struct HWAccelConfig {
    AVHWDeviceType deviceType;
    HWAccelType accelType;
    const char* name;
};

static const HWAccelConfig g_hwAccelConfigs[] = {
#if defined(__APPLE__)
    { AV_HWDEVICE_TYPE_VIDEOTOOLBOX, HWAccelType::VideoToolbox, "VideoToolbox" },
#elif defined(_WIN32)
    { AV_HWDEVICE_TYPE_D3D11VA, HWAccelType::D3D11VA, "D3D11VA" },
    { AV_HWDEVICE_TYPE_DXVA2, HWAccelType::DXVA2, "DXVA2" },
    { AV_HWDEVICE_TYPE_CUDA, HWAccelType::CUDA, "CUDA" },
    { AV_HWDEVICE_TYPE_QSV, HWAccelType::QSV, "Intel QSV" },
#elif defined(__linux__)
    { AV_HWDEVICE_TYPE_VAAPI, HWAccelType::VAAPI, "VAAPI" },
    { AV_HWDEVICE_TYPE_CUDA, HWAccelType::CUDA, "CUDA" },
#endif
};

static AVPixelFormat GetHWFormat(AVCodecContext* ctx, const AVPixelFormat* pixFmts) {
    AVPixelFormat hwFmt = *static_cast<AVPixelFormat*>(ctx->opaque);
    for (const AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == hwFmt) {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

VideoDecoder::VideoDecoder()
    : _formatContext(nullptr)
    , _codecContext(nullptr)
    , _videoStream(nullptr)
    , _swsContext(nullptr)
    , _frame(nullptr)
    , _swFrame(nullptr)
    , _packet(nullptr)
    , _hwDeviceCtx(nullptr)
    , _videoStreamIndex(-1)
    , _width(0)
    , _height(0)
    , _fps(0.0)
    , _duration(0.0)
    , _currentTime(0.0)
    , _hwAccelType(HWAccelType::None)
    , _hwPixelFormat(AV_PIX_FMT_NONE)
    , _swsSourceFormat(AV_PIX_FMT_NONE)
    , _swsSourceWidth(0)
    , _swsSourceHeight(0)
{
}

VideoDecoder::~VideoDecoder() {
    Close();
}

const char* VideoDecoder::GetHWAccelName() const {
    switch (_hwAccelType) {
        case HWAccelType::VideoToolbox: return "VideoToolbox";
        case HWAccelType::CUDA: return "CUDA";
        case HWAccelType::VAAPI: return "VAAPI";
        case HWAccelType::D3D11VA: return "D3D11VA";
        case HWAccelType::DXVA2: return "DXVA2";
        case HWAccelType::QSV: return "Intel QSV";
        default: return "Software";
    }
}

bool VideoDecoder::InitHardwareDecoder(const AVCodec* codec) {
    for (const auto& config : g_hwAccelConfigs) {
        for (int i = 0;; i++) {
            const AVCodecHWConfig* hwConfig = avcodec_get_hw_config(codec, i);
            if (!hwConfig) break;
            
            if (hwConfig->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                hwConfig->device_type == config.deviceType) {
                
                int ret = av_hwdevice_ctx_create(&_hwDeviceCtx, config.deviceType, nullptr, nullptr, 0);
                if (ret >= 0) {
                    _codecContext->hw_device_ctx = av_buffer_ref(_hwDeviceCtx);
                    _hwAccelType = config.accelType;
                    _hwPixelFormat = hwConfig->pix_fmt;
                    _codecContext->opaque = &_hwPixelFormat;
                    _codecContext->get_format = GetHWFormat;
                    TVK_LOG_INFO("Hardware acceleration enabled: {}", config.name);
                    return true;
                }
            }
        }
    }
    return false;
}

bool VideoDecoder::TransferHWFrame(AVFrame* hwFrame, AVFrame* swFrame) {
    if (hwFrame->format == _hwPixelFormat) {
        av_frame_unref(swFrame);
        if (av_hwframe_transfer_data(swFrame, hwFrame, 0) < 0) {
            return false;
        }
        swFrame->pts = hwFrame->pts;
        return true;
    }
    return false;
}

bool VideoDecoder::Open(const std::string& filepath) {
    Close();

    if (avformat_open_input(&_formatContext, filepath.c_str(), nullptr, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to open video file: {}", filepath);
        return false;
    }

    if (avformat_find_stream_info(_formatContext, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to find stream information");
        Close();
        return false;
    }

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

    AVCodecParameters* codecParams = _videoStream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        TVK_LOG_ERROR("Unsupported codec");
        Close();
        return false;
    }

    _codecContext = avcodec_alloc_context3(codec);
    if (!_codecContext) {
        TVK_LOG_ERROR("Failed to allocate codec context");
        Close();
        return false;
    }

    if (avcodec_parameters_to_context(_codecContext, codecParams) < 0) {
        TVK_LOG_ERROR("Failed to copy codec parameters");
        Close();
        return false;
    }

    if (!InitHardwareDecoder(codec)) {
        TVK_LOG_INFO("Hardware acceleration not available, using software decode");
        _hwAccelType = HWAccelType::None;
    }

    if (avcodec_open2(_codecContext, codec, nullptr) < 0) {
        TVK_LOG_ERROR("Failed to open codec");
        Close();
        return false;
    }

    _width = _codecContext->width;
    _height = _codecContext->height;
    
    if (_videoStream->avg_frame_rate.den != 0) {
        _fps = av_q2d(_videoStream->avg_frame_rate);
    } else {
        _fps = 30.0;
    }

    if (_formatContext->duration != AV_NOPTS_VALUE) {
        _duration = (double)_formatContext->duration / AV_TIME_BASE;
    } else if (_videoStream->duration != AV_NOPTS_VALUE) {
        _duration = (double)_videoStream->duration * av_q2d(_videoStream->time_base);
    }

    _frame = av_frame_alloc();
    _swFrame = av_frame_alloc();
    _packet = av_packet_alloc();

    if (!_frame || !_swFrame || !_packet) {
        TVK_LOG_ERROR("Failed to allocate frame or packet");
        Close();
        return false;
    }

    _currentTime = 0.0;

    TVK_LOG_INFO("Video opened successfully:");
    TVK_LOG_INFO("  Resolution: {}x{}", _width, _height);
    TVK_LOG_INFO("  FPS: {:.2f}", _fps);
    TVK_LOG_INFO("  Duration: {:.2f} seconds", _duration);
    TVK_LOG_INFO("  Decoder: {}", GetHWAccelName());

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
        int ret = av_read_frame(_formatContext, _packet);
        
        if (ret < 0) {
            return false;
        }

        if (_packet->stream_index != _videoStreamIndex) {
            av_packet_unref(_packet);
            continue;
        }

        ret = avcodec_send_packet(_codecContext, _packet);
        av_packet_unref(_packet);

        if (ret < 0) {
            TVK_LOG_ERROR("Error sending packet to decoder");
            return false;
        }

        ret = avcodec_receive_frame(_codecContext, _frame);

        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            TVK_LOG_ERROR("Error receiving frame from decoder");
            return false;
        }

        AVFrame* sourceFrame = _frame;
        
        if (_hwAccelType != HWAccelType::None && _frame->format == _hwPixelFormat) {
            if (!TransferHWFrame(_frame, _swFrame)) {
                av_frame_unref(_frame);
                continue;
            }
            sourceFrame = _swFrame;
        }

        outFrame.width = _width;
        outFrame.height = _height;
        outFrame.data.resize(_width * _height * 4);
        memset(outFrame.data.data(), 0, outFrame.data.size());

        if (_frame->pts != AV_NOPTS_VALUE) {
            outFrame.timestamp = _frame->pts * av_q2d(_videoStream->time_base);
            _currentTime = outFrame.timestamp;
        } else {
            outFrame.timestamp = _currentTime;
        }

        AVPixelFormat srcFormat = (AVPixelFormat)sourceFrame->format;
        int srcWidth = sourceFrame->width;
        int srcHeight = sourceFrame->height;
        
        if (!_swsContext || _swsSourceFormat != srcFormat || 
            _swsSourceWidth != srcWidth || _swsSourceHeight != srcHeight) {
            if (_swsContext) {
                sws_freeContext(_swsContext);
            }
            _swsContext = sws_getContext(
                srcWidth, srcHeight, srcFormat,
                _width, _height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            _swsSourceFormat = srcFormat;
            _swsSourceWidth = srcWidth;
            _swsSourceHeight = srcHeight;
        }

        if (!_swsContext) {
            av_frame_unref(_frame);
            if (sourceFrame == _swFrame) av_frame_unref(_swFrame);
            return false;
        }

        uint8_t* dest[4] = { outFrame.data.data(), nullptr, nullptr, nullptr };
        int destLinesize[4] = { _width * 4, 0, 0, 0 };

        sws_scale(
            _swsContext,
            sourceFrame->data, sourceFrame->linesize,
            0, srcHeight,
            dest, destLinesize
        );

        av_frame_unref(_frame);
        if (sourceFrame == _swFrame) av_frame_unref(_swFrame);
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
    
    if (_swsContext) {
        sws_freeContext(_swsContext);
        _swsContext = nullptr;
        _swsSourceFormat = AV_PIX_FMT_NONE;
        _swsSourceWidth = 0;
        _swsSourceHeight = 0;
    }

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
    AVFrame* tempSwFrame = av_frame_alloc();
    AVPacket* tempPacket = av_packet_alloc();
    if (!tempFrame || !tempSwFrame || !tempPacket) {
        if (tempFrame) av_frame_free(&tempFrame);
        if (tempSwFrame) av_frame_free(&tempSwFrame);
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
        av_frame_free(&tempSwFrame);
        av_packet_free(&tempPacket);
        return false;
    }

    AVFrame* sourceFrame = tempFrame;
    if (_hwAccelType != HWAccelType::None && tempFrame->format == _hwPixelFormat) {
        if (av_hwframe_transfer_data(tempSwFrame, tempFrame, 0) >= 0) {
            sourceFrame = tempSwFrame;
        }
    }

    float aspect = (float)_width / (float)_height;
    int thumbW = maxWidth;
    int thumbH = (int)(maxWidth / aspect);
    if (thumbH > maxHeight) {
        thumbH = maxHeight;
        thumbW = (int)(maxHeight * aspect);
    }

    SwsContext* thumbSws = sws_getContext(
        _width, _height, (AVPixelFormat)sourceFrame->format,
        thumbW, thumbH, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!thumbSws) {
        av_frame_free(&tempFrame);
        av_frame_free(&tempSwFrame);
        av_packet_free(&tempPacket);
        return false;
    }

    outFrame.width = thumbW;
    outFrame.height = thumbH;
    outFrame.data.resize(thumbW * thumbH * 4);
    outFrame.timestamp = timeSeconds;

    uint8_t* dest[4] = { outFrame.data.data(), nullptr, nullptr, nullptr };
    int destLinesize[4] = { thumbW * 4, 0, 0, 0 };

    sws_scale(thumbSws, sourceFrame->data, sourceFrame->linesize, 0, _height, dest, destLinesize);

    sws_freeContext(thumbSws);
    av_frame_free(&tempFrame);
    av_frame_free(&tempSwFrame);
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

    if (_swFrame) {
        av_frame_free(&_swFrame);
        _swFrame = nullptr;
    }

    if (_packet) {
        av_packet_free(&_packet);
        _packet = nullptr;
    }

    if (_codecContext) {
        avcodec_free_context(&_codecContext);
        _codecContext = nullptr;
    }

    if (_hwDeviceCtx) {
        av_buffer_unref(&_hwDeviceCtx);
        _hwDeviceCtx = nullptr;
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
    _hwAccelType = HWAccelType::None;
    _hwPixelFormat = AV_PIX_FMT_NONE;
    _swsSourceFormat = AV_PIX_FMT_NONE;
    _swsSourceWidth = 0;
    _swsSourceHeight = 0;
}

} // namespace tvk_media
