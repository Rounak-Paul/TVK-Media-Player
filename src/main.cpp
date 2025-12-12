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
        config.width = 1600;
        config.height = 900;
        config.mode = tvk::AppMode::GUI;
        config.vsync = true;
        
        app.Run(config);
    } catch (const std::exception& e) {
        TVK_LOG_FATAL("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
