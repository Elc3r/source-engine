# AVFoundation startup video

Native startup movie playback for 64-bit macOS (Apple Silicon and Intel) using AVFoundation and
AVKit. Implements `IVideoSubSystem002` as `libvideo_quicktime.dylib` in the
existing QUICKTIME subsystem slot.

Supports local MOV, MP4 and M4V files with audio, aspect-preserving playback in
the SDL Cocoa window, windowed and fullscreen modes, and skipping with Escape,
Space or Return. The engine's `-novid` option bypasses startup playback.

Video materials, recording, looping, preload and pause modes are not supported.

The AVFoundation implementation is selected for ARM64 and x86_64 macOS builds.
The original `video/video_quicktime` sources and VPC project are preserved for
legacy 32-bit OS X; 32-bit builds retain the original video window handle path.

Playback, audio, fullscreen and skipping have been verified with Portal on
Apple Silicon. Intel playback has not been tested.
