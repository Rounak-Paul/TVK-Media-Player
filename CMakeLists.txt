cmake_minimum_required(VERSION 3.20)
project(TVKMediaPlayer VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug, Release, etc.)" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/${CMAKE_BUILD_TYPE})

foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CONFIG})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_SOURCE_DIR}/lib/${CONFIG})
endforeach()

# Add TinyVK library
add_subdirectory(vendors/tinyvk)

# Add OpenAL Soft
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendors/openal-soft)

# Find FFmpeg
if(WIN32)
    if(NOT DEFINED FFMPEG_ROOT)
        file(GLOB WINGET_FFMPEG_PATHS "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Packages/Gyan.FFmpeg.Shared*/ffmpeg-*-full_build-shared")
        
        set(FFMPEG_SEARCH_PATHS
            ${WINGET_FFMPEG_PATHS}
            "C:/ffmpeg"
            "C:/Program Files/ffmpeg"
            "$ENV{USERPROFILE}/ffmpeg"
            "$ENV{LOCALAPPDATA}/ffmpeg"
        )
        
        foreach(SEARCH_PATH ${FFMPEG_SEARCH_PATHS})
            if(EXISTS "${SEARCH_PATH}/include/libavcodec/avcodec.h")
                set(FFMPEG_ROOT "${SEARCH_PATH}")
                message(STATUS "Found FFmpeg at: ${FFMPEG_ROOT}")
                break()
            endif()
        endforeach()
        
        if(NOT DEFINED FFMPEG_ROOT)
            message(FATAL_ERROR "FFmpeg not found. Please:\n"
                "1. Download FFmpeg from https://www.gyan.dev/ffmpeg/builds/ (full shared build)\n"
                "2. Extract to C:/ffmpeg\n"
                "3. Or set FFMPEG_ROOT: cmake -DFFMPEG_ROOT=C:/path/to/ffmpeg ..")
        endif()
    endif()
    
    find_path(FFMPEG_INCLUDE_DIR
        NAMES libavcodec/avcodec.h
        PATHS ${FFMPEG_ROOT}/include
        NO_DEFAULT_PATH
    )
    
    find_library(AVCODEC_LIBRARY
        NAMES avcodec
        PATHS ${FFMPEG_ROOT}/lib
        NO_DEFAULT_PATH
    )
    
    find_library(AVFORMAT_LIBRARY
        NAMES avformat
        PATHS ${FFMPEG_ROOT}/lib
        NO_DEFAULT_PATH
    )
    
    find_library(AVUTIL_LIBRARY
        NAMES avutil
        PATHS ${FFMPEG_ROOT}/lib
        NO_DEFAULT_PATH
    )
    
    find_library(SWSCALE_LIBRARY
        NAMES swscale
        PATHS ${FFMPEG_ROOT}/lib
        NO_DEFAULT_PATH
    )
    
    find_library(SWRESAMPLE_LIBRARY
        NAMES swresample
        PATHS ${FFMPEG_ROOT}/lib
        NO_DEFAULT_PATH
    )
    
    if(NOT FFMPEG_INCLUDE_DIR OR NOT AVCODEC_LIBRARY)
        message(FATAL_ERROR "FFmpeg development files not found at ${FFMPEG_ROOT}. "
            "Make sure you downloaded the 'full shared' build with include/ and lib/ directories.")
    endif()
    
    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
    set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY})
    set(FFMPEG_LIBRARY_DIRS ${FFMPEG_ROOT}/lib)
    set(FFMPEG_CFLAGS_OTHER "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED 
        libavcodec 
        libavformat 
        libavutil 
        libswscale
        libswresample
    )
endif()

# Media Player executable
add_executable(tvk-media-player
    src/main.cpp
    src/video_decoder.cpp
    src/audio_decoder.cpp
    src/video_effects.cpp
    src/media_player.cpp
)

target_include_directories(tvk-media-player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendors/openal-soft/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(tvk-media-player PRIVATE
    tinyvk
    OpenAL
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
    
    add_custom_command(TARGET tvk-media-player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:tvk-media-player>
        COMMENT "Copying OpenAL32.dll to bin"
    )
    
    file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
    foreach(DLL ${FFMPEG_DLLS})
        add_custom_command(TARGET tvk-media-player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DLL}
                $<TARGET_FILE_DIR:tvk-media-player>
            COMMENT "Copying ${DLL} to bin"
        )
    endforeach()
elseif(UNIX)
    target_compile_definitions(tvk-media-player PRIVATE __linux__)
endif()
