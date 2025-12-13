/**
 * @file media_player.cpp
 * @brief Implementation of the media player application
 */

#include "media_player.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <tinyvk/assets/icons_font_awesome.h>
#include <GLFW/glfw3.h>

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
    DrawVideoView();
    DrawMenuBar();
    DrawControls();
    HandleWindowResizing();
}

void MediaPlayer::OnStop() {
    TVK_LOG_INFO("Media Player stopped");
    
    if (_videoTexture) {
        _videoTexture.reset();
    }
    
    if (_decoder) {
        _decoder->Close();
    }
}

void MediaPlayer::DrawMenuBar() {
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
    
    if (ImGui::BeginMainMenuBar()) {
        HandleWindowDragging();

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

        DrawWindowControls();
        
        ImGui::EndMainMenuBar();
    }
    
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
    float controlsHeight = 120.0f;
    float padding = 20.0f;
    
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + padding, 
                                    viewport->Pos.y + viewport->Size.y - controlsHeight - padding));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - padding * 2, controlsHeight));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoNav;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));
    
    ImGui::Begin("##Controls", nullptr, flags);
    
    float buttonWidth = 40.0f;
    float buttonHeight = 30.0f;
    
    if (_isPlaying) {
        if (ImGui::Button(ICON_FA_PAUSE, ImVec2(buttonWidth, buttonHeight))) {
            TogglePlayPause();
        }
    } else {
        if (ImGui::Button(ICON_FA_PLAY, ImVec2(buttonWidth, buttonHeight))) {
            TogglePlayPause();
        }
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button(ICON_FA_STOP, ImVec2(buttonWidth, buttonHeight))) {
        _isPlaying = false;
        _pausedAtTime = 0.0;
        SeekTo(0.0);
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button(ICON_FA_FOLDER_OPEN, ImVec2(buttonWidth, buttonHeight))) {
        OpenFile();
    }
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("##volume", &_volume, 0.0f, 1.0f, ICON_FA_VOLUME_HIGH " %.0f%%");
    
    ImGui::SameLine();
    
    if (_hasVideo) {
        double duration = _decoder->GetDuration();
        double currentTime = _isPlaying 
            ? ElapsedTime() - _videoStartTime + _pausedAtTime 
            : _pausedAtTime;
        
        if (currentTime > duration) {
            currentTime = duration;
            _isPlaying = false;
        }
        
        int currentMin = (int)currentTime / 60;
        int currentSec = (int)currentTime % 60;
        int durationMin = (int)duration / 60;
        int durationSec = (int)duration % 60;
        
        ImGui::Text("%02d:%02d / %02d:%02d", currentMin, currentSec, durationMin, durationSec);
    }
    
    DrawTimeline();
    
    if (_hasVideo) {
        ImGui::Text("%s - %dx%d @ %.1f FPS", 
            _currentFilePath.c_str(),
            _decoder->GetWidth(), 
            _decoder->GetHeight(), 
            _decoder->GetFPS());
    }
    
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void MediaPlayer::DrawTimeline() {
    if (!_hasVideo) return;
    
    double duration = _decoder->GetDuration();
    double currentTime = _isPlaying 
        ? ElapsedTime() - _videoStartTime + _pausedAtTime 
        : _pausedAtTime;
    
    if (currentTime > duration) {
        currentTime = duration;
        _isPlaying = false;
    }
    
    if (!_isSeeking) {
        _seekBarValue = (float)(currentTime / duration);
    }
    
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##timeline", &_seekBarValue, 0.0f, 1.0f, "")) {
        _isSeeking = true;
    }
    
    if (_isSeeking && !ImGui::IsItemActive()) {
        double newTime = _seekBarValue * duration;
        SeekTo(newTime);
        _isSeeking = false;
    }
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

} // namespace tvk_media
