/**
 * @file media_player.h
 * @brief Media player application using TinyVK and FFmpeg
 */

#pragma once

#include <tinyvk/tinyvk.h>
#include "video_decoder.h"
#include <memory>
#include <string>

namespace tvk_media {

/**
 * @brief Main media player application
 */
class MediaPlayer : public tvk::App {
public:
    MediaPlayer();
    ~MediaPlayer() override = default;

protected:
    void OnStart() override;
    void OnUpdate() override;
    void OnUI() override;
    void OnStop() override;

private:
    void DrawMenuBar();
    void DrawVideoView();
    void DrawControls();
    void DrawTimeline();
    void HandleWindowDragging();
    void HandleWindowResizing();
    void DrawWindowControls();
    
    void OpenFile();
    void TogglePlayPause();
    void UpdateVideo();
    void SeekTo(double timeSeconds);

    // Video decoding
    std::unique_ptr<VideoDecoder> _decoder;
    std::unique_ptr<VideoDecoder> _thumbnailDecoder;
    VideoFrame _currentFrame;
    tvk::Ref<tvk::Texture> _videoTexture;
    
    // Thumbnail preview
    VideoFrame _thumbnailFrame;
    tvk::Ref<tvk::Texture> _thumbnailTexture;
    double _lastThumbnailTime;
    bool _showThumbnail;
    
    // Playback state
    bool _isPlaying;
    bool _hasVideo;
    double _videoStartTime;
    double _pausedAtTime;
    float _volume;
    
    // UI state
    std::string _currentFilePath;
    bool _showControls;
    float _seekBarValue;
    bool _isSeeking;

    // Window state for custom title bar
    bool _isDragging;
    bool _isResizing;
    int _resizeDir;
    float _dragOffsetX;
    float _dragOffsetY;
    float _lastMouseX;
    float _lastMouseY;
    bool _isCustomMaximized;
    int _prevWinX;
    int _prevWinY;
    unsigned int _prevWinW;
    unsigned int _prevWinH;
};

} // namespace tvk_media
