/**
 * @file media_player.cpp
 * @brief Implementation of the media player application
 */

#include "media_player.h"
#include <imgui.h>
#include <tinyvk/assets/icons_font_awesome.h>

namespace tvk_media {

MediaPlayer::MediaPlayer()
    : _decoder(nullptr)
    , _videoTexture(nullptr)
    , _isPlaying(false)
    , _hasVideo(false)
    , _videoStartTime(0.0)
    , _pausedAtTime(0.0)
    , _volume(1.0f)
    , _showControls(true)
    , _seekBarValue(0.0f)
    , _isSeeking(false)
{
}

void MediaPlayer::OnStart() {
    TVK_LOG_INFO("Media Player started");
    _decoder = std::make_unique<VideoDecoder>();
}

void MediaPlayer::OnUpdate() {
    // Handle keyboard shortcuts
    if (tvk::Input::IsKeyPressed(tvk::Key::Escape)) {
        Quit();
    }
    
    if (tvk::Input::IsKeyPressed(tvk::Key::Space)) {
        TogglePlayPause();
    }
    
    if (tvk::Input::IsKeyPressed(tvk::Key::O) && 
        (tvk::Input::IsKeyDown(tvk::Key::LeftControl) || tvk::Input::IsKeyDown(tvk::Key::RightControl))) {
        OpenFile();
    }

    // Update video playback
    if (_isPlaying && _hasVideo) {
        UpdateVideo();
    }
}

void MediaPlayer::OnUI() {
    // Set up docking
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, viewport);

    DrawMenuBar();
    DrawVideoView();
    DrawControls();
}

void MediaPlayer::OnStop() {
    TVK_LOG_INFO("Media Player stopped");
    if (_decoder) {
        _decoder->Close();
    }
}

void MediaPlayer::DrawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open", "Ctrl+O")) {
                OpenFile();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " Exit", "Esc")) {
                Quit();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Playback")) {
            const char* playPauseLabel = _isPlaying ? ICON_FA_PAUSE " Pause" : ICON_FA_PLAY " Play";
            if (ImGui::MenuItem(playPauseLabel, "Space", nullptr, _hasVideo)) {
                TogglePlayPause();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_STOP " Stop", nullptr, nullptr, _hasVideo)) {
                _isPlaying = false;
                _pausedAtTime = 0.0;
                SeekTo(0.0);
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Controls", nullptr, &_showControls);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem(ICON_FA_INFO " About")) {
                TVK_LOG_INFO("TVK Media Player v1.0.0");
            }
            ImGui::EndMenu();
        }
        
        // Display info on the right side
        if (_hasVideo && _decoder) {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 300);
            ImGui::Text("Resolution: %dx%d | FPS: %.1f", 
                       _decoder->GetWidth(), 
                       _decoder->GetHeight(), 
                       _decoder->GetFPS());
        }
        
        ImGui::EndMainMenuBar();
    }
}

void MediaPlayer::DrawVideoView() {
    ImGui::Begin("Video", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    
    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    
    if (_hasVideo && _videoTexture) {
        // Calculate aspect ratio and scale
        float videoAspect = (float)_decoder->GetWidth() / (float)_decoder->GetHeight();
        float windowAspect = windowSize.x / windowSize.y;
        
        ImVec2 imageSize;
        ImVec2 imagePos;
        
        if (windowAspect > videoAspect) {
            // Window is wider - fit to height
            imageSize.y = windowSize.y;
            imageSize.x = imageSize.y * videoAspect;
            imagePos.x = (windowSize.x - imageSize.x) * 0.5f;
            imagePos.y = 0;
        } else {
            // Window is taller - fit to width
            imageSize.x = windowSize.x;
            imageSize.y = imageSize.x / videoAspect;
            imagePos.x = 0;
            imagePos.y = (windowSize.y - imageSize.y) * 0.5f;
        }
        
        // Center the video
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + imagePos.x, 
                                   ImGui::GetCursorPosY() + imagePos.y));
        ImGui::Image(_videoTexture->GetImGuiTextureID(), imageSize);
    } else {
        // Show placeholder
        ImVec2 textSize = ImGui::CalcTextSize(ICON_FA_VIDEO " No video loaded");
        ImGui::SetCursorPos(ImVec2(
            (windowSize.x - textSize.x) * 0.5f,
            (windowSize.y - textSize.y) * 0.5f
        ));
        ImGui::TextDisabled(ICON_FA_VIDEO " No video loaded");
        ImGui::SetCursorPos(ImVec2(
            (windowSize.x - 200) * 0.5f,
            (windowSize.y + textSize.y) * 0.5f + 20
        ));
        ImGui::TextDisabled("Press Ctrl+O to open a video file");
    }
    
    ImGui::End();
}

void MediaPlayer::DrawControls() {
    if (!_showControls) return;
    
    ImGui::Begin("Controls", &_showControls);
    
    // Playback controls
    ImGui::Text("Playback");
    ImGui::Separator();
    
    // Play/Pause button
    if (_isPlaying) {
        if (ImGui::Button(ICON_FA_PAUSE " Pause", ImVec2(100, 0))) {
            TogglePlayPause();
        }
    } else {
        if (ImGui::Button(ICON_FA_PLAY " Play", ImVec2(100, 0))) {
            TogglePlayPause();
        }
    }
    
    ImGui::SameLine();
    
    // Stop button
    if (ImGui::Button(ICON_FA_STOP " Stop", ImVec2(100, 0))) {
        _isPlaying = false;
        _pausedAtTime = 0.0;
        SeekTo(0.0);
    }
    
    ImGui::SameLine();
    
    // Open file button
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open", ImVec2(100, 0))) {
        OpenFile();
    }
    
    ImGui::Spacing();
    DrawTimeline();
    
    // Volume control
    ImGui::Spacing();
    ImGui::Text("Volume");
    ImGui::Separator();
    ImGui::SliderFloat("##volume", &_volume, 0.0f, 1.0f, "%.2f");
    
    // File info
    if (_hasVideo) {
        ImGui::Spacing();
        ImGui::Text("File Info");
        ImGui::Separator();
        ImGui::Text("File: %s", _currentFilePath.c_str());
        ImGui::Text("Resolution: %dx%d", _decoder->GetWidth(), _decoder->GetHeight());
        ImGui::Text("FPS: %.2f", _decoder->GetFPS());
        ImGui::Text("Duration: %.2f seconds", _decoder->GetDuration());
    }
    
    ImGui::End();
}

void MediaPlayer::DrawTimeline() {
    if (!_hasVideo) return;
    
    double duration = _decoder->GetDuration();
    double currentTime = _isPlaying 
        ? ElapsedTime() - _videoStartTime + _pausedAtTime 
        : _pausedAtTime;
    
    // Clamp current time
    if (currentTime > duration) {
        currentTime = duration;
        _isPlaying = false;
    }
    
    // Update seek bar
    if (!_isSeeking) {
        _seekBarValue = (float)(currentTime / duration);
    }
    
    // Timeline slider
    ImGui::Text("Timeline");
    if (ImGui::SliderFloat("##timeline", &_seekBarValue, 0.0f, 1.0f, "%.3f")) {
        _isSeeking = true;
    }
    
    // Check if user released the slider
    if (_isSeeking && !ImGui::IsItemActive()) {
        double newTime = _seekBarValue * duration;
        SeekTo(newTime);
        _isSeeking = false;
    }
    
    // Time display
    int currentMin = (int)currentTime / 60;
    int currentSec = (int)currentTime % 60;
    int durationMin = (int)duration / 60;
    int durationSec = (int)duration % 60;
    
    ImGui::Text("%02d:%02d / %02d:%02d", currentMin, currentSec, durationMin, durationSec);
}

void MediaPlayer::OpenFile() {
    auto filepath = tvk::FileDialog::OpenFile({{"Video Files", "mp4,avi,mkv,mov,wmv,flv,webm"}});
    
    if (filepath.has_value()) {
        if (_decoder->Open(filepath.value())) {
            _currentFilePath = filepath.value();
            _hasVideo = true;
            _isPlaying = false;
            _pausedAtTime = 0.0;
            _seekBarValue = 0.0f;
            
            // Decode first frame
            if (_decoder->DecodeNextFrame(_currentFrame)) {
                // Create texture from first frame
                tvk::TextureSpec spec;
                spec.width = _currentFrame.width;
                spec.height = _currentFrame.height;
                spec.format = tvk::TextureFormat::RGBA8;
                
                _videoTexture = tvk::Texture::Create(
                    GetRenderer(),
                    _currentFrame.data.data(),
                    _currentFrame.width,
                    _currentFrame.height,
                    spec
                );
                
                if (_videoTexture) {
                    _videoTexture->BindToImGui();
                }
            }
            
            TVK_LOG_INFO("Opened video file: {}", filepath.value());
        } else {
            TVK_LOG_ERROR("Failed to open video file: {}", filepath.value());
        }
    }
}

void MediaPlayer::TogglePlayPause() {
    if (!_hasVideo) return;
    
    _isPlaying = !_isPlaying;
    
    if (_isPlaying) {
        _videoStartTime = ElapsedTime() - _pausedAtTime;
        TVK_LOG_INFO("Playback started");
    } else {
        _pausedAtTime = ElapsedTime() - _videoStartTime;
        TVK_LOG_INFO("Playback paused at {:.2f}s", _pausedAtTime);
    }
}

void MediaPlayer::UpdateVideo() {
    if (!_decoder || !_hasVideo) return;
    
    double currentPlaybackTime = ElapsedTime() - _videoStartTime + _pausedAtTime;
    double frameDuration = 1.0 / _decoder->GetFPS();
    
    // Check if we need to decode the next frame
    if (currentPlaybackTime >= _currentFrame.timestamp + frameDuration) {
        if (_decoder->DecodeNextFrame(_currentFrame)) {
            // Update texture with new frame
            if (_videoTexture) {
                _videoTexture->SetData(
                    _currentFrame.data.data(),
                    _currentFrame.width,
                    _currentFrame.height
                );
            }
        } else {
            // End of video - loop back to start
            _isPlaying = false;
            _pausedAtTime = _decoder->GetDuration();
            TVK_LOG_INFO("Playback finished");
        }
    }
}

void MediaPlayer::SeekTo(double timeSeconds) {
    if (!_decoder || !_hasVideo) return;
    
    if (_decoder->Seek(timeSeconds)) {
        _pausedAtTime = timeSeconds;
        
        if (_isPlaying) {
            _videoStartTime = ElapsedTime() - _pausedAtTime;
        }
        
        // Decode frame at new position
        if (_decoder->DecodeNextFrame(_currentFrame)) {
            if (_videoTexture) {
                _videoTexture->SetData(
                    _currentFrame.data.data(),
                    _currentFrame.width,
                    _currentFrame.height
                );
            }
        }
        
        TVK_LOG_INFO("Seeked to {:.2f}s", timeSeconds);
    }
}

} // namespace tvk_media
