/**
 * @file media_player.cpp
 * @brief Implementation of the media player application
 */

#include "media_player.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <GLFW/glfw3.h>
#include <cmath>

namespace tvk_media {

MediaPlayer::MediaPlayer()
    : _decoder(nullptr)
    , _thumbnailDecoder(nullptr)
    , _videoTexture(nullptr)
    , _thumbnailTexture(nullptr)
    , _lastThumbnailTime(-1.0)
    , _showThumbnail(false)
    , _isPlaying(false)
    , _hasVideo(false)
    , _videoStartTime(0.0)
    , _pausedAtTime(0.0)
    , _volume(1.0f)
    , _showControls(true)
    , _seekBarValue(0.0f)
    , _isSeeking(false)
    , _isDragging(false)
    , _isResizing(false)
    , _resizeDir(0)
    , _dragOffsetX(0.0f)
    , _dragOffsetY(0.0f)
    , _lastMouseX(0.0f)
    , _lastMouseY(0.0f)
    , _isCustomMaximized(false)
    , _prevWinX(0)
    , _prevWinY(0)
    , _prevWinW(1600)
    , _prevWinH(900)
    , _showColorWindow(false)
    , _showFiltersWindow(false)
    , _showPostProcessWindow(false)
{
}

void MediaPlayer::OnStart() {
    TVK_LOG_INFO("Media Player started");
    _decoder = std::make_unique<VideoDecoder>();
    _thumbnailDecoder = std::make_unique<VideoDecoder>();
    _audioDecoder = std::make_unique<AudioDecoder>();
    _videoEffects = std::make_unique<VideoEffects>();
    _videoEffects->Init(GetRenderer());
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
    
    // Update audio
    if (_audioDecoder && _audioDecoder->HasAudio()) {
        _audioDecoder->Update();
    }
}

void MediaPlayer::OnUI() {
    DrawVideoView();
    DrawMenuBar();
    DrawControls();
    HandleWindowResizing();
    
    if (_showColorWindow) {
        DrawColorAdjustmentsWindow();
    }
    if (_showFiltersWindow) {
        DrawFiltersWindow();
    }
    if (_showPostProcessWindow) {
        DrawPostProcessWindow();
    }
}

void MediaPlayer::OnStop() {
    TVK_LOG_INFO("Media Player stopped");
    
    if (_videoEffects) {
        _videoEffects->Cleanup();
    }
    
    if (_videoTexture) {
        _videoTexture.reset();
    }
    
    if (_thumbnailTexture) {
        _thumbnailTexture.reset();
    }
    
    if (_decoder) {
        _decoder->Close();
    }
    
    if (_thumbnailDecoder) {
        _thumbnailDecoder->Close();
    }
    
    if (_audioDecoder) {
        _audioDecoder->Close();
    }
}

void MediaPlayer::DrawMenuBar() {
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.01f, 0.02f, 0.03f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.01f, 0.02f, 0.03f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    if (ImGui::BeginMainMenuBar()) {
        HandleWindowDragging();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open", "Ctrl+O")) {
                OpenFile();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Esc")) {
                Quit();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Playback")) {
            const char* playPauseLabel = _isPlaying ? "Pause" : "Play";
            if (ImGui::MenuItem(playPauseLabel, "Space", nullptr, _hasVideo)) {
                TogglePlayPause();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Stop", nullptr, nullptr, _hasVideo)) {
                _isPlaying = false;
                _pausedAtTime = 0.0;
                SeekTo(0.0);
            }
            ImGui::Separator();
            ImGui::MenuItem("Show Controls", nullptr, &_showControls);
            ImGui::EndMenu();
        }
        
        
        if (ImGui::BeginMenu("Video")) {
            ImGui::MenuItem("Color Adjustments", nullptr, &_showColorWindow);
            ImGui::MenuItem("Filters", nullptr, &_showFiltersWindow);
            ImGui::MenuItem("Post Processing", nullptr, &_showPostProcessWindow);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset All Effects")) {
                _videoEffects->ResetAll();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                TVK_LOG_INFO("TVK Media Player v1.0.0");
            }
            ImGui::EndMenu();
        }

        DrawWindowControls();
        
        ImGui::EndMainMenuBar();
    }
    
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(2);
}

void MediaPlayer::DrawVideoView() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    
    ImGui::Begin("##VideoBackground", nullptr, flags);
    
    ImVec2 windowSize = viewport->Size;
    
    if (_hasVideo && _videoTexture) {
        float videoAspect = (float)_decoder->GetWidth() / (float)_decoder->GetHeight();
        float windowAspect = windowSize.x / windowSize.y;
        
        ImVec2 imageSize;
        ImVec2 imagePos;
        
        if (windowAspect > videoAspect) {
            imageSize.y = windowSize.y;
            imageSize.x = imageSize.y * videoAspect;
            imagePos.x = (windowSize.x - imageSize.x) * 0.5f;
            imagePos.y = 0;
        } else {
            imageSize.x = windowSize.x;
            imageSize.y = imageSize.x / videoAspect;
            imagePos.x = 0;
            imagePos.y = (windowSize.y - imageSize.y) * 0.5f;
        }
        
        ImGui::SetCursorPos(imagePos);
        ImGui::Image(_videoTexture->GetImGuiTextureID(), imageSize);
    } else {
        ImVec2 textSize = ImGui::CalcTextSize(ICON_FA_VIDEO " No video loaded");
        ImGui::SetCursorPos(ImVec2(
            (windowSize.x - textSize.x) * 0.5f,
            (windowSize.y - textSize.y) * 0.5f
        ));
        ImGui::TextDisabled(ICON_FA_VIDEO " No video loaded");
        
        ImVec2 subTextSize = ImGui::CalcTextSize("Press Ctrl+O to open a video file");
        ImGui::SetCursorPos(ImVec2(
            (windowSize.x - subTextSize.x) * 0.5f,
            (windowSize.y + textSize.y) * 0.5f + 10
        ));
        ImGui::TextDisabled("Press Ctrl+O to open a video file");
    }
    
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MediaPlayer::DrawControls() {
    if (!_showControls) return;
    
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float barHeight = 52.0f;
    float margin = 24.0f;
    float barWidth = viewport->Size.x - margin * 2;
    float barY = viewport->Pos.y + viewport->Size.y - barHeight - 20.0f;
    float barX = viewport->Pos.x + margin;
    
    ImGui::SetNextWindowPos(ImVec2(barX, barY));
    ImGui::SetNextWindowSize(ImVec2(barWidth, barHeight));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoNav;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.01f, 0.02f, 0.03f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGui::Begin("##Controls", nullptr, flags);
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 s = ImGui::GetWindowSize();
    
    double currentTime = 0.0;
    double duration = 1.0;
    
    if (_hasVideo) {
        duration = _decoder->GetDuration();
        currentTime = _isPlaying 
            ? ElapsedTime() - _videoStartTime 
            : _pausedAtTime;
        if (currentTime > duration) { currentTime = duration; _isPlaying = false; }
        if (currentTime < 0) currentTime = 0;
        if (!_isSeeking) _seekBarValue = (float)(currentTime / duration);
    }
    
    float pad = 16.0f;
    float btnSize = 24.0f;
    float centerY = s.y * 0.5f;
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btnSize);
    
    ImVec4 normCol(1, 1, 1, 0.6f);
    ImVec4 hovCol(1, 1, 1, 1.0f);
    
    ImGui::SetCursorPos(ImVec2(pad, centerY - btnSize * 0.5f));
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    bool playHov = ImGui::IsMouseHoveringRect(btnPos, ImVec2(btnPos.x + btnSize, btnPos.y + btnSize));
    ImGui::PushStyleColor(ImGuiCol_Text, playHov ? hovCol : normCol);
    if (_isPlaying) {
        if (ImGui::Button(ICON_FA_PAUSE, ImVec2(btnSize, btnSize))) TogglePlayPause();
    } else {
        if (ImGui::Button(ICON_FA_PLAY, ImVec2(btnSize, btnSize))) TogglePlayPause();
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine(0, 12);
    btnPos = ImGui::GetCursorScreenPos();
    bool prevHov = ImGui::IsMouseHoveringRect(btnPos, ImVec2(btnPos.x + btnSize, btnPos.y + btnSize));
    ImGui::PushStyleColor(ImGuiCol_Text, prevHov ? hovCol : normCol);
    if (ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(btnSize, btnSize))) {
        SeekTo(0.0);
        _pausedAtTime = 0.0;
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine(0, 12);
    btnPos = ImGui::GetCursorScreenPos();
    bool nextHov = ImGui::IsMouseHoveringRect(btnPos, ImVec2(btnPos.x + btnSize, btnPos.y + btnSize));
    ImGui::PushStyleColor(ImGuiCol_Text, nextHov ? hovCol : normCol);
    if (ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(btnSize, btnSize))) {
        if (_hasVideo) SeekTo(_decoder->GetDuration());
    }
    ImGui::PopStyleColor();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    
    float timeTextWidth = 50.0f;
    float timeGap = 12.0f;
    float timeX = pad + btnSize * 3 + 24;
    
    float volWidth = 60.0f;
    float volIconW = 20.0f;
    float rightEnd = s.x - pad;
    float volSliderX = rightEnd - volWidth;
    float volIconX = volSliderX - volIconW - 8;
    
    float sliderStart = timeX + timeTextWidth + timeGap;
    float sliderEnd = volIconX - timeTextWidth - timeGap - 8;
    float sliderWidth = sliderEnd - sliderStart;
    
    if (_hasVideo) {
        int cm = (int)currentTime / 60, cs = (int)currentTime % 60;
        char buf[24];
        snprintf(buf, sizeof(buf), "%d:%02d", cm, cs);
        float tw = ImGui::CalcTextSize(buf).x;
        ImGui::SetCursorPos(ImVec2(sliderStart - timeGap - tw, centerY - ImGui::GetTextLineHeight() * 0.5f));
        ImGui::TextColored(ImVec4(1, 1, 1, 0.9f), "%s", buf);
    }
    
    if (sliderWidth > 100) {
        float sliderH = 4.0f;
        float sliderY = centerY - sliderH * 0.5f;
        ImVec2 sPos = ImVec2(p.x + sliderStart, p.y + sliderY);
        
        dl->AddRectFilled(sPos, ImVec2(sPos.x + sliderWidth, sPos.y + sliderH), 
                          IM_COL32(255, 255, 255, 40), sliderH * 0.5f);
        
        bool hover = _hasVideo && ImGui::IsMouseHoveringRect(
            ImVec2(sPos.x - 4, sPos.y - 10),
            ImVec2(sPos.x + sliderWidth + 4, sPos.y + sliderH + 10));
        
        float hoverValue = 0.0f;
        if (hover || _isSeeking) {
            float mx = ImGui::GetMousePos().x - sPos.x;
            hoverValue = mx / sliderWidth;
            if (hoverValue < 0) hoverValue = 0;
            if (hoverValue > 1) hoverValue = 1;
        }
        
        if (_hasVideo) {
            float prog = sliderWidth * _seekBarValue;
            dl->AddRectFilled(sPos, ImVec2(sPos.x + prog, sPos.y + sliderH), 
                              IM_COL32(255, 100, 50, 255), sliderH * 0.5f);
            
            if (hover || _isSeeking) {
                dl->AddCircleFilled(ImVec2(sPos.x + prog, sPos.y + sliderH * 0.5f), 
                                    6.0f, IM_COL32(255, 255, 255, 255));
            }
        }
        
        if (_hasVideo && (hover || _isSeeking)) {
            double previewTime = _isSeeking ? (_seekBarValue * duration) : (hoverValue * duration);
            
            if (fabs(previewTime - _lastThumbnailTime) > 0.5 || _lastThumbnailTime < 0) {
                if (_thumbnailDecoder->GetThumbnailAt(previewTime, _thumbnailFrame, 160, 90)) {
                    _lastThumbnailTime = previewTime;
                    _showThumbnail = true;
                    
                    tvk::TextureSpec spec;
                    spec.width = _thumbnailFrame.width;
                    spec.height = _thumbnailFrame.height;
                    spec.format = tvk::TextureFormat::RGBA8;
                    
                    _thumbnailTexture = tvk::Texture::Create(
                        GetRenderer(),
                        _thumbnailFrame.data.data(),
                        _thumbnailFrame.width,
                        _thumbnailFrame.height,
                        spec
                    );
                    
                    if (_thumbnailTexture) {
                        _thumbnailTexture->BindToImGui();
                    }
                }
            }
            
            if (_showThumbnail && _thumbnailTexture) {
                ImDrawList* fgDl = ImGui::GetForegroundDrawList();
                float thumbW = (float)_thumbnailFrame.width;
                float thumbH = (float)_thumbnailFrame.height;
                float previewX = _isSeeking ? _seekBarValue : hoverValue;
                float thumbX = sPos.x + previewX * sliderWidth - thumbW * 0.5f;
                float thumbY = sPos.y - thumbH - 30.0f;
                
                if (thumbX < sPos.x) thumbX = sPos.x;
                if (thumbX + thumbW > sPos.x + sliderWidth) thumbX = sPos.x + sliderWidth - thumbW;
                
                int tm = (int)previewTime / 60, ts = (int)previewTime % 60;
                char timeBuf[24];
                snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", tm, ts);
                ImVec2 textSize = ImGui::CalcTextSize(timeBuf);
                float textX = thumbX + (thumbW - textSize.x) * 0.5f;
                float textY = thumbY - textSize.y - 4.0f;
                
                fgDl->AddRectFilled(ImVec2(textX - 4, textY - 2), 
                                    ImVec2(textX + textSize.x + 4, textY + textSize.y + 2),
                                    IM_COL32(0, 0, 0, 200), 4.0f);
                fgDl->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), timeBuf);
                
                fgDl->AddRectFilled(ImVec2(thumbX - 2, thumbY - 2), 
                                    ImVec2(thumbX + thumbW + 2, thumbY + thumbH + 2),
                                    IM_COL32(30, 30, 30, 255), 4.0f);
                fgDl->AddImage(_thumbnailTexture->GetImGuiTextureID(),
                               ImVec2(thumbX, thumbY), ImVec2(thumbX + thumbW, thumbY + thumbH));
            }
        } else {
            _showThumbnail = false;
            _lastThumbnailTime = -1.0;
        }
        
        ImGui::SetCursorPos(ImVec2(sliderStart - 4, sliderY - 10));
        ImGui::InvisibleButton("##seek", ImVec2(sliderWidth + 8, 24));
        
        bool clicked = ImGui::IsItemClicked();
        bool active = ImGui::IsItemActive();
        
        if (_hasVideo && (clicked || active)) {
            float mx = ImGui::GetMousePos().x - sPos.x;
            _seekBarValue = mx / sliderWidth;
            if (_seekBarValue < 0) _seekBarValue = 0;
            if (_seekBarValue > 1) _seekBarValue = 1;
            _isSeeking = true;
        }
        
        if (_hasVideo && _isSeeking && !active) {
            SeekTo(_seekBarValue * duration);
            _isSeeking = false;
            _lastThumbnailTime = -1.0;
        }
    }
    
    if (_hasVideo) {
        int dm = (int)duration / 60, ds = (int)duration % 60;
        char buf[24];
        snprintf(buf, sizeof(buf), "%d:%02d", dm, ds);
        ImGui::SetCursorPos(ImVec2(sliderEnd + timeGap, centerY - ImGui::GetTextLineHeight() * 0.5f));
        ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), "%s", buf);
    }
    
    const char* vIcon = _volume <= 0 ? ICON_FA_VOLUME_XMARK : 
                        _volume < 0.5f ? ICON_FA_VOLUME_LOW : ICON_FA_VOLUME_HIGH;
    ImVec2 vIconPos = ImVec2(p.x + volIconX, p.y + centerY - ImGui::GetTextLineHeight() * 0.5f);
    bool vIconHov = ImGui::IsMouseHoveringRect(vIconPos, ImVec2(vIconPos.x + volIconW, vIconPos.y + ImGui::GetTextLineHeight()));
    ImGui::SetCursorPos(ImVec2(volIconX, centerY - ImGui::GetTextLineHeight() * 0.5f));
    ImGui::TextColored(vIconHov ? ImVec4(1, 1, 1, 1.0f) : ImVec4(1, 1, 1, 0.6f), "%s", vIcon);
    
    float volH = 3.0f;
    float volY = centerY - volH * 0.5f;
    ImVec2 vPos = ImVec2(p.x + volSliderX, p.y + volY);
    
    dl->AddRectFilled(vPos, ImVec2(vPos.x + volWidth, vPos.y + volH), 
                      IM_COL32(255, 255, 255, 40), volH * 0.5f);
    dl->AddRectFilled(vPos, ImVec2(vPos.x + volWidth * _volume, vPos.y + volH), 
                      IM_COL32(255, 255, 255, 200), volH * 0.5f);
    
    bool vHov = ImGui::IsMouseHoveringRect(
        ImVec2(vPos.x - 4, vPos.y - 8), ImVec2(vPos.x + volWidth + 4, vPos.y + volH + 8));
    if (vHov) {
        dl->AddCircleFilled(ImVec2(vPos.x + volWidth * _volume, vPos.y + volH * 0.5f), 
                            4.0f, IM_COL32(255, 255, 255, 255));
    }
    
    ImGui::SetCursorPos(ImVec2(volSliderX - 4, volY - 8));
    ImGui::InvisibleButton("##vol", ImVec2(volWidth + 8, 20));
    if (ImGui::IsItemActive()) {
        float mx = ImGui::GetMousePos().x - vPos.x;
        _volume = mx / volWidth;
        if (_volume < 0) _volume = 0;
        if (_volume > 1) _volume = 1;
        if (_audioDecoder && _audioDecoder->HasAudio()) {
            _audioDecoder->SetVolume(_volume);
        }
    }
    
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(3);
}

void MediaPlayer::DrawTimeline() {
}

void MediaPlayer::OpenFile() {
    auto filepath = tvk::FileDialog::OpenFile({{"Video Files", "mp4,avi,mkv,mov,wmv,flv,webm"}});
    
    if (filepath.has_value()) {
        if (_videoTexture) {
            _videoTexture.reset();
        }
        if (_thumbnailTexture) {
            _thumbnailTexture.reset();
        }
        
        if (_decoder->Open(filepath.value())) {
            _thumbnailDecoder->Open(filepath.value());
            _audioDecoder->Open(filepath.value());
            
            _currentFilePath = filepath.value();
            _hasVideo = true;
            _isPlaying = false;
            _pausedAtTime = 0.0;
            _seekBarValue = 0.0f;
            _lastThumbnailTime = -1.0;
            _showThumbnail = false;
            
            if (_decoder->DecodeNextFrame(_currentFrame)) {
                tvk::TextureSpec spec;
                spec.width = _currentFrame.width;
                spec.height = _currentFrame.height;
                spec.format = tvk::TextureFormat::RGBA8;
                spec.generateMipmaps = false;
                spec.storageUsage = true;
                
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
        if (_audioDecoder->HasAudio()) {
            _audioDecoder->Play();
        }
        TVK_LOG_INFO("Playback started");
    } else {
        _pausedAtTime = ElapsedTime() - _videoStartTime;
        if (_audioDecoder->HasAudio()) {
            _audioDecoder->Pause();
        }
        TVK_LOG_INFO("Playback paused at {}s", _pausedAtTime);
    }
}

void MediaPlayer::UpdateVideo() {
    if (!_decoder || !_hasVideo) return;
    
    double currentPlaybackTime = ElapsedTime() - _videoStartTime;
    double frameDuration = 1.0 / _decoder->GetFPS();
    
    if (currentPlaybackTime >= _currentFrame.timestamp + frameDuration) {
        if (_decoder->DecodeNextFrame(_currentFrame)) {
            if (_videoTexture) {
                _videoTexture->SetData(
                    _currentFrame.data.data(),
                    _currentFrame.width,
                    _currentFrame.height
                );
                
                if (_videoEffects && _videoEffects->HasActiveEffects()) {
                    _videoEffects->ProcessFrame(_videoTexture.get());
                }
            }
        } else {
            _isPlaying = false;
            _pausedAtTime = _decoder->GetDuration();
            if (_audioDecoder->HasAudio()) {
                _audioDecoder->Stop();
            }
            TVK_LOG_INFO("Playback finished");
        }
    }
}

void MediaPlayer::SeekTo(double timeSeconds) {
    if (!_decoder || !_hasVideo) return;
    
    if (_decoder->Seek(timeSeconds)) {
        if (_audioDecoder->HasAudio()) {
            _audioDecoder->Seek(timeSeconds);
        }
        
        if (_decoder->DecodeNextFrame(_currentFrame)) {
            if (_videoTexture) {
                _videoTexture->SetData(
                    _currentFrame.data.data(),
                    _currentFrame.width,
                    _currentFrame.height
                );
            }
        }
        
        _pausedAtTime = _currentFrame.timestamp;
        
        if (_isPlaying) {
            _videoStartTime = ElapsedTime() - _currentFrame.timestamp;
        }
        
        TVK_LOG_INFO("Seeked to {}s", timeSeconds);
    }
}

void MediaPlayer::HandleWindowDragging() {
    tvk::Window* window = GetWindow();
    if (!window) return;

    GLFWwindow* glfw_win = window->GetNativeHandle();
    
    double cursor_x, cursor_y;
    glfwGetCursorPos(glfw_win, &cursor_x, &cursor_y);
    
    tvk::i32 win_x, win_y;
    window->GetPosition(win_x, win_y);
    
    float screen_mouse_x = static_cast<float>(win_x) + static_cast<float>(cursor_x);
    float screen_mouse_y = static_cast<float>(win_y) + static_cast<float>(cursor_y);
    
    ImVec2 menu_bar_size = ImVec2(ImGui::GetWindowSize().x, ImGui::GetFrameHeight());

    bool mouse_over_menu_bar = (cursor_x >= 0 &&
                                cursor_x <= static_cast<double>(menu_bar_size.x) - 120 &&
                                cursor_y >= 0 &&
                                cursor_y <= static_cast<double>(menu_bar_size.y));

    if (mouse_over_menu_bar && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered()) {
        _isDragging = true;
        _dragOffsetX = screen_mouse_x;
        _dragOffsetY = screen_mouse_y;
        _prevWinX = win_x;
        _prevWinY = win_y;
    }

    if (_isDragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (_isCustomMaximized) {
                tvk::Extent2D ext = window->GetExtent();
                float ratio = static_cast<float>(cursor_x) / static_cast<float>(ext.width);
                _isCustomMaximized = false;
                window->SetSize(_prevWinW, _prevWinH);
                window->GetPosition(win_x, win_y);
                _dragOffsetX = screen_mouse_x;
                _dragOffsetY = screen_mouse_y;
                _prevWinX = win_x;
                _prevWinY = win_y;
                _dragOffsetX = static_cast<float>(win_x) + ratio * static_cast<float>(_prevWinW);
            }
            int new_x = _prevWinX + static_cast<int>(screen_mouse_x - _dragOffsetX);
            int new_y = _prevWinY + static_cast<int>(screen_mouse_y - _dragOffsetY);
            window->SetPosition(new_x, new_y);
        } else {
            _isDragging = false;
        }
    }
}

void MediaPlayer::HandleWindowResizing() {
    tvk::Window* window = GetWindow();
    if (!window) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 win_pos = viewport->Pos;
    ImVec2 win_size = viewport->Size;
    ImVec2 mouse_pos = ImGui::GetMousePos();
    const float border_thickness = 6.0f;

    ImRect right_border(win_pos.x + win_size.x - border_thickness, win_pos.y,
                        win_pos.x + win_size.x, win_pos.y + win_size.y);
    ImRect bottom_border(win_pos.x, win_pos.y + win_size.y - border_thickness,
                         win_pos.x + win_size.x, win_pos.y + win_size.y);
    ImRect corner(win_pos.x + win_size.x - border_thickness,
                  win_pos.y + win_size.y - border_thickness,
                  win_pos.x + win_size.x, win_pos.y + win_size.y);

    if (!_isResizing) {
        if (corner.Contains(mouse_pos)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                _isResizing = true;
                _resizeDir = 3;
                _isCustomMaximized = false;
            }
        } else if (right_border.Contains(mouse_pos)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                _isResizing = true;
                _resizeDir = 1;
                _isCustomMaximized = false;
            }
        } else if (bottom_border.Contains(mouse_pos)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                _isResizing = true;
                _resizeDir = 2;
                _isCustomMaximized = false;
            }
        }
    }

    if (_isResizing) {
        if (_resizeDir == 1)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        else if (_resizeDir == 2)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        else if (_resizeDir == 3)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float delta_x = mouse_pos.x - _lastMouseX;
            float delta_y = mouse_pos.y - _lastMouseY;
            tvk::Extent2D ext = window->GetExtent();
            tvk::u32 width = ext.width;
            tvk::u32 height = ext.height;

            if (_resizeDir == 1 || _resizeDir == 3) {
                int new_width = static_cast<int>(width) + static_cast<int>(delta_x);
                width = new_width > 400 ? static_cast<tvk::u32>(new_width) : 400;
            }
            if (_resizeDir == 2 || _resizeDir == 3) {
                int new_height = static_cast<int>(height) + static_cast<int>(delta_y);
                height = new_height > 300 ? static_cast<tvk::u32>(new_height) : 300;
            }

            window->SetSize(width, height);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            _isResizing = false;
            _resizeDir = 0;
        }
    }

    _lastMouseX = mouse_pos.x;
    _lastMouseY = mouse_pos.y;
}

void MediaPlayer::DrawWindowControls() {
    tvk::Window* window = GetWindow();
    if (!window) return;

    const float icon_size = ImGui::GetFrameHeight();
    const float spacing = 4.0f;
    const float total_w = 3 * icon_size + 2 * spacing + 8.0f;
    float start_x = ImGui::GetWindowWidth() - total_w;

    ImGui::SetCursorPosX(start_x);

    ImVec4 normal_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 hover_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    ImVec4 close_hover_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

    auto draw_icon_button = [&](const char* id, const char* icon, ImVec4 hovered_col, auto on_click) {
        ImGui::PushID(id);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(icon_size, icon_size));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();
        
        ImVec4 text_col = hovered ? hovered_col : normal_color;
        ImVec2 text_size = ImGui::CalcTextSize(icon);
        ImVec2 text_pos = ImVec2(
            pos.x + (icon_size - text_size.x) * 0.5f,
            pos.y + (icon_size - text_size.y) * 0.5f
        );
        ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::ColorConvertFloat4ToU32(text_col), icon);
        
        if (clicked) {
            on_click();
        }
        ImGui::PopID();
    };

    draw_icon_button("minimize_btn", ICON_FA_MINUS, hover_color, [&]() {
        window->Iconify();
    });

    ImGui::SameLine(0, spacing);

    const char* maximize_icon = _isCustomMaximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE;
    draw_icon_button("maximize_btn", maximize_icon, hover_color, [&]() {
        if (!_isCustomMaximized) {
            window->GetPosition(_prevWinX, _prevWinY);
            tvk::Extent2D ext = window->GetExtent();
            _prevWinW = ext.width;
            _prevWinH = ext.height;

            int wx = _prevWinX + static_cast<int>(_prevWinW) / 2;
            int wy = _prevWinY + static_cast<int>(_prevWinH) / 2;
            int monitor_count = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);

            int work_x = 0, work_y = 0, work_w = 0, work_h = 0;
            bool found = false;
            for (int i = 0; i < monitor_count; ++i) {
                int mx, my, mw, mh;
                glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
                if (wx >= mx && wx < mx + mw && wy >= my && wy < my + mh) {
                    work_x = mx; work_y = my; work_w = mw; work_h = mh;
                    found = true;
                    break;
                }
            }

            if (!found) {
                GLFWmonitor* primary = glfwGetPrimaryMonitor();
                if (primary)
                    glfwGetMonitorWorkarea(primary, &work_x, &work_y, &work_w, &work_h);
            }

            if (work_w > 0 && work_h > 0) {
                window->SetPosition(work_x, work_y);
                window->SetSize(static_cast<tvk::u32>(work_w), static_cast<tvk::u32>(work_h));
                _isCustomMaximized = true;
            }
        } else {
            window->SetPosition(_prevWinX, _prevWinY);
            window->SetSize(_prevWinW, _prevWinH);
            _isCustomMaximized = false;
        }
    });

    ImGui::SameLine(0, spacing);

    draw_icon_button("close_btn", ICON_FA_XMARK, close_hover_color, [&]() {
        Quit();
    });
}

void MediaPlayer::DrawColorAdjustmentsWindow() {
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Color Adjustments", &_showColorWindow)) {
        ColorAdjustments& adj = _videoEffects->GetColorAdjustments();
        
        ImGui::Text("Basic");
        ImGui::Separator();
        
        ImGui::SliderFloat("Brightness", &adj.brightness, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Contrast", &adj.contrast, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Gamma", &adj.gamma, 0.1f, 3.0f, "%.2f");
        ImGui::SliderFloat("Exposure", &adj.exposure, -3.0f, 3.0f, "%.2f");
        
        ImGui::Spacing();
        ImGui::Text("Color");
        ImGui::Separator();
        
        ImGui::SliderFloat("Hue Shift", &adj.hue, -0.5f, 0.5f, "%.2f");
        ImGui::SliderFloat("Saturation", &adj.saturation, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Temperature", &adj.temperature, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Tint", &adj.tint, -1.0f, 1.0f, "%.2f");
        
        ImGui::Spacing();
        ImGui::Text("Tone");
        ImGui::Separator();
        
        ImGui::SliderFloat("Shadows", &adj.shadows, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Highlights", &adj.highlights, -1.0f, 1.0f, "%.2f");
        
        ImGui::Spacing();
        if (ImGui::Button("Reset##Color", ImVec2(-1, 0))) {
            adj.Reset();
        }
    }
    ImGui::End();
}

void MediaPlayer::DrawFiltersWindow() {
    ImGui::SetNextWindowSize(ImVec2(280, 300), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Filters", &_showFiltersWindow)) {
        FilterSettings& flt = _videoEffects->GetFilterSettings();
        
        const char* filterNames[] = {
            "None",
            "Grayscale",
            "Sepia",
            "Invert",
            "Posterize",
            "Solarize",
            "Threshold",
            "Sharpen",
            "Edge Detect"
        };
        
        int currentFilter = static_cast<int>(flt.type);
        if (ImGui::Combo("Filter", &currentFilter, filterNames, IM_ARRAYSIZE(filterNames))) {
            flt.type = static_cast<FilterType>(currentFilter);
        }
        
        ImGui::Spacing();
        
        if (flt.type == FilterType::Grayscale || flt.type == FilterType::Sepia || flt.type == FilterType::Invert) {
            ImGui::SliderFloat("Strength", &flt.strength, 0.0f, 1.0f, "%.2f");
        }
        
        if (flt.type == FilterType::Posterize) {
            ImGui::SliderInt("Levels", &flt.levels, 2, 16);
        }
        
        if (flt.type == FilterType::Solarize || flt.type == FilterType::Threshold) {
            ImGui::SliderFloat("Threshold", &flt.threshold, 0.0f, 1.0f, "%.2f");
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Reset##Filter", ImVec2(-1, 0))) {
            flt.Reset();
        }
    }
    ImGui::End();
}

void MediaPlayer::DrawPostProcessWindow() {
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Post Processing", &_showPostProcessWindow)) {
        PostProcessSettings& pp = _videoEffects->GetPostProcess();
        
        ImGui::Text("Bloom");
        ImGui::Separator();
        
        ImGui::SliderFloat("Bloom Intensity", &pp.bloom, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Bloom Threshold", &pp.bloomThreshold, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Bloom Spread", &pp.bloomRadius, 1.0f, 6.0f, "%.0f");
        
        ImGui::Spacing();
        ImGui::Text("Effects");
        ImGui::Separator();
        
        ImGui::SliderFloat("Chromatic Aberration", &pp.chromaticAberration, 0.0f, 1.0f, "%.2f");
        
        ImGui::Spacing();
        ImGui::Text("Vignette");
        ImGui::Separator();
        
        ImGui::SliderFloat("Vignette", &pp.vignette, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette Size", &pp.vignetteSize, 0.0f, 1.0f, "%.2f");
        
        ImGui::Spacing();
        ImGui::Text("Film");
        ImGui::Separator();
        
        ImGui::SliderFloat("Film Grain", &pp.filmGrain, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Scanlines", &pp.scanlines, 0.0f, 1.0f, "%.2f");
        
        ImGui::Spacing();
        ImGui::Text("Vintage");
        ImGui::Separator();
        
        ImGui::Checkbox("Enable Vintage", &pp.vintageEnabled);
        if (pp.vintageEnabled) {
            ImGui::SliderFloat("Vintage Strength", &pp.vintageStrength, 0.0f, 1.0f, "%.2f");
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Reset##PostProcess", ImVec2(-1, 0))) {
            pp.Reset();
        }
    }
    ImGui::End();
}

} // namespace tvk_media
