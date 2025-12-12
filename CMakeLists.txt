cmake_minimum_required(VERSION 3.20)
project(TVKMediaPlayer VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add TinyVK library
add_subdirectory(vendors/tinyvk)

# Find FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED 
    libavcodec 
    libavformat 
    libavutil 
    libswscale
)

# Media Player executable
add_executable(tvk-media-player
    src/main.cpp
    src/video_decoder.cpp
    src/media_player.cpp
)

target_include_directories(tvk-media-player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(tvk-media-player PRIVATE
    tinyvk
    ${FFMPEG_LIBRARIES}
)

target_link_directories(tvk-media-player PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_compile_options(tvk-media-player PRIVATE
    ${FFMPEG_CFLAGS_OTHER}
)

# Platform-specific settings
if(APPLE)
    target_compile_definitions(tvk-media-player PRIVATE __APPLE__)
elseif(WIN32)
    target_compile_definitions(tvk-media-player PRIVATE _WIN32)
elseif(UNIX)
    target_compile_definitions(tvk-media-player PRIVATE __linux__)
endif()

# Set output directory
set_target_properties(tvk-media-player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
