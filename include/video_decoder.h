/**
 * @file video_decoder.h
 * @brief Video decoder using FFmpeg for media playback
 */

#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

#include <string>
#include <vector>

namespace tvk_media {

enum class HWAccelType {
    None,
    VideoToolbox,
    CUDA,
    VAAPI,
    D3D11VA,
    DXVA2,
    QSV
};

struct VideoFrame {
    std::vector<uint8_t> data;
    int width;
    int height;
    double timestamp;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool Open(const std::string& filepath);
    void Close();
    bool DecodeNextFrame(VideoFrame& outFrame);
    bool Seek(double timeSeconds);
    bool GetThumbnailAt(double timeSeconds, VideoFrame& outFrame, int maxWidth, int maxHeight);

    double GetDuration() const { return _duration; }
    double GetCurrentTime() const { return _currentTime; }
    int GetWidth() const { return _width; }
    int GetHeight() const { return _height; }
    double GetFPS() const { return _fps; }
    bool IsOpen() const { return _formatContext != nullptr; }
    bool IsHardwareAccelerated() const { return _hwAccelType != HWAccelType::None; }
    HWAccelType GetHWAccelType() const { return _hwAccelType; }
    const char* GetHWAccelName() const;

private:
    bool InitHardwareDecoder(const AVCodec* codec);
    bool TransferHWFrame(AVFrame* hwFrame, AVFrame* swFrame);
    
    AVFormatContext* _formatContext;
    AVCodecContext* _codecContext;
    AVStream* _videoStream;
    SwsContext* _swsContext;
    AVFrame* _frame;
    AVFrame* _swFrame;
    AVPacket* _packet;
    AVBufferRef* _hwDeviceCtx;

    int _videoStreamIndex;
    int _width;
    int _height;
    double _fps;
    double _duration;
    double _currentTime;
    HWAccelType _hwAccelType;
    AVPixelFormat _hwPixelFormat;
    AVPixelFormat _swsSourceFormat;
    int _swsSourceWidth;
    int _swsSourceHeight;

    void Cleanup();
};

} // namespace tvk_media
