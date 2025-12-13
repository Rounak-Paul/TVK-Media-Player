/**
 * @file main.cpp
 * @brief Entry point for TVK Media Player
 */

#include "media_player.h"
#include <tinyvk/tinyvk.h>

int main() {
    try {
        tvk_media::MediaPlayer app;
        
        tvk::AppConfig config;
        config.title = "TVK Media Player";
        config.width = 1280;
        config.height = 720;
        config.mode = tvk::AppMode::GUI;
        config.vsync = true;
        config.decorated = false;
        
        app.Run(config);
    } catch (const std::exception& e) {
        TVK_LOG_FATAL("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
