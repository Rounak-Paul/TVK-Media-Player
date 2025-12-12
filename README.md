# TVK Media Player

A basic video player application built with TinyVK and FFmpeg, inspired by VLC Media Player.

## Features

- **Video Playback**: Play various video formats (MP4, AVI, MKV, MOV, WMV, FLV, WebM)
- **Playback Controls**: Play, pause, stop, and seek functionality
- **Timeline Slider**: Visual timeline with current playback position
- **File Browser**: Open video files through native file dialog
- **Video Information**: Display resolution, FPS, and duration
- **Keyboard Shortcuts**:
  - `Space`: Play/Pause
  - `Ctrl+O`: Open file
  - `Esc`: Exit application
- **Modern UI**: Built with ImGui and Font Awesome icons
- **Dockable Interface**: Flexible window layout

## Architecture

The application is designed with upgradability in mind:

- **VideoDecoder** (`video_decoder.h/cpp`): Handles video decoding using FFmpeg
  - Supports seeking, frame extraction, and format detection
  - Easily extendable for audio support
  
- **MediaPlayer** (`media_player.h/cpp`): Main application logic
  - Built on TinyVK's App framework
  - Modular design for adding features (playlists, subtitles, etc.)
  
- **TinyVK Framework**: Handles Vulkan rendering, windowing, and UI
  - Abstracts complex graphics programming
  - Focus on application logic

## Prerequisites

### macOS
```bash
# Install FFmpeg
brew install ffmpeg

# Install required build tools
brew install cmake ninja
```

### Linux (Ubuntu/Debian)
```bash
# Install FFmpeg development libraries
sudo apt-get update
sudo apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    cmake \
    ninja-build \
    pkg-config
```

### Windows
1. Download FFmpeg development libraries from https://ffmpeg.org/download.html
2. Install CMake from https://cmake.org/download/
3. Configure FFmpeg paths in CMakeLists.txt if needed

## Building

```bash
# Clone the repository (if not already done)
cd TVK-Media-Player

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -G Ninja

# Build
ninja

# Run the application
./bin/tvk-media-player
```

## Usage

1. Launch the application
2. Click **File → Open** or press `Ctrl+O` to open a video file
3. Use the playback controls:
   - Play/Pause button or `Space` key
   - Stop button to reset playback
   - Timeline slider to seek through the video
4. Adjust volume using the volume slider

## Project Structure

```
TVK-Media-Player/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── include/
│   ├── video_decoder.h     # Video decoding interface
│   └── media_player.h      # Media player application
├── src/
│   ├── main.cpp           # Application entry point
│   ├── video_decoder.cpp  # FFmpeg-based video decoder
│   └── media_player.cpp   # Media player implementation
└── vendors/
    └── tinyvk/            # TinyVK framework (submodule)
```

## Future Enhancements

The codebase is structured to easily add:

- **Audio Support**: Extend VideoDecoder to handle audio streams
- **Playlist Management**: Add playlist support in MediaPlayer
- **Subtitle Support**: Parse and display subtitle tracks
- **Video Filters**: Add color correction, effects, etc.
- **Fullscreen Mode**: Toggle fullscreen playback
- **Playback Speed Control**: Variable speed playback
- **Recent Files**: Track and quick-access recent videos
- **Thumbnail Preview**: Show thumbnail when hovering over timeline
- **Hardware Acceleration**: GPU-accelerated video decoding
- **Streaming Support**: Network streaming protocols

## Dependencies

- **TinyVK**: Vulkan-based rendering framework with ImGui integration
- **FFmpeg**: Video decoding library (libavcodec, libavformat, libavutil, libswscale)
- **Vulkan**: Graphics API (via TinyVK)
- **GLFW**: Window management (via TinyVK)
- **ImGui**: Immediate mode GUI (via TinyVK)

## License

See LICENSE file for details.

## Troubleshooting

### FFmpeg not found
If CMake cannot find FFmpeg, ensure pkg-config is installed and FFmpeg libraries are in your system path.

**macOS:**
```bash
brew install pkg-config
export PKG_CONFIG_PATH="/usr/local/opt/ffmpeg/lib/pkgconfig"
```

**Linux:**
```bash
sudo apt-get install pkg-config
```

### Video won't play
- Ensure the video file is in a supported format
- Check the console for error messages
- Try converting the video with: `ffmpeg -i input.mp4 -c:v libx264 output.mp4`

### Build errors
- Ensure all submodules are initialized: `git submodule update --init --recursive`
- Check that Vulkan SDK is installed (required by TinyVK)
- Verify CMake version is 3.20 or higher

## Contributing

This is a basic implementation designed for easy extension. Feel free to:
- Add new features
- Improve performance
- Fix bugs
- Enhance the UI

## Credits

- Built with [TinyVK](https://github.com/Rounak-Paul/tinyvk) by Rounak-Paul
- Video decoding powered by [FFmpeg](https://ffmpeg.org/)
- UI framework: [Dear ImGui](https://github.com/ocornut/imgui)
- Icons: [Font Awesome](https://fontawesome.com/)
