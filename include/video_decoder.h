/**
 * @file video_decoder.h
 * @brief Video decoder using FFmpeg for media playback
 */

#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <string>
#include <memory>
#include <vector>

namespace tvk_media {

/**
 * @brief Stores a decoded video frame
 */
struct VideoFrame {
    std::vector<uint8_t> data;  // RGBA pixel data
    int width;
    int height;
    double timestamp;  // Presentation timestamp in seconds
};

/**
 * @brief Video decoder class using FFmpeg
 */
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // Prevent copying
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    /**
     * @brief Open a video file for decoding
     * @param filepath Path to the video file
     * @return true if successful, false otherwise
     */
    bool Open(const std::string& filepath);

    /**
     * @brief Close the currently open video file
     */
    void Close();

    /**
     * @brief Decode the next frame
     * @param outFrame Output frame data
     * @return true if a frame was decoded, false if EOF or error
     */
    bool DecodeNextFrame(VideoFrame& outFrame);

    /**
     * @brief Seek to a specific time in the video
     * @param timeSeconds Time in seconds
     * @return true if successful, false otherwise
     */
    bool Seek(double timeSeconds);

    /**
     * @brief Get a frame at a specific timestamp without affecting playback position
     * @param timeSeconds Time in seconds
     * @param outFrame Output frame data (scaled to thumbnail size)
     * @param maxWidth Maximum thumbnail width
     * @param maxHeight Maximum thumbnail height
     * @return true if successful, false otherwise
     */
    bool GetThumbnailAt(double timeSeconds, VideoFrame& outFrame, int maxWidth, int maxHeight);

    /**
     * @brief Get the total duration of the video
     * @return Duration in seconds
     */
    double GetDuration() const { return _duration; }

    /**
     * @brief Get the current playback position
     * @return Current time in seconds
     */
    double GetCurrentTime() const { return _currentTime; }

    /**
     * @brief Get the video width
     * @return Video width in pixels
     */
    int GetWidth() const { return _width; }

    /**
     * @brief Get the video height
     * @return Video height in pixels
     */
    int GetHeight() const { return _height; }

    /**
     * @brief Get the frames per second
     * @return FPS value
     */
    double GetFPS() const { return _fps; }

    /**
     * @brief Check if a video is currently open
     * @return true if a video is open, false otherwise
     */
    bool IsOpen() const { return _formatContext != nullptr; }

private:
    AVFormatContext* _formatContext;
    AVCodecContext* _codecContext;
    AVStream* _videoStream;
    SwsContext* _swsContext;
    AVFrame* _frame;
    AVPacket* _packet;

    int _videoStreamIndex;
    int _width;
    int _height;
    double _fps;
    double _duration;
    double _currentTime;

    void Cleanup();
};

} // namespace tvk_media
