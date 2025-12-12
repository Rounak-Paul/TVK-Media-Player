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
    
    void OpenFile();
    void TogglePlayPause();
    void UpdateVideo();
    void SeekTo(double timeSeconds);

    // Video decoding
    std::unique_ptr<VideoDecoder> _decoder;
    VideoFrame _currentFrame;
    tvk::Ref<tvk::Texture> _videoTexture;
    
    // Playback state
    bool _isPlaying;
    bool _hasVideo;
    double _videoStartTime;  // When playback started
    double _pausedAtTime;    // Time when video was paused
    float _volume;
    
    // UI state
    std::string _currentFilePath;
    bool _showControls;
    float _seekBarValue;
    bool _isSeeking;
};

} // namespace tvk_media
