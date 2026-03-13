## Radiary FFmpeg bundle

Radiary uses a standalone `ffmpeg.exe` for MP4 and MOV export. The intended bundle is a custom minimal Windows build with only the features the app currently uses:

- `file` and `pipe` protocols
- `rawvideo` demuxer
- `mp4` and `mov` muxers
- `libx264` encoder
- `swscale` for BGRA to `yuv420p` conversion

### Build

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .tools/build-minimal-ffmpeg.ps1
```

Prerequisites:

- MSYS2 with a working `mingw64` toolchain
- or MSYS2 `ucrt64` toolchain
- `git` on PATH
- `tar` on PATH
- the required MSYS2 packages for FFmpeg/x264 builds, such as `make`, `pkgconf`, `nasm`, and `mingw-w64-x86_64-gcc`

The build script downloads FFmpeg from `ffmpeg.org`, clones `x264` from VideoLAN, builds a minimal static `ffmpeg.exe`, and stages the result into this folder.

### Packaging

By default, CMake embeds `third_party/ffmpeg/bin/ffmpeg.exe` into `Radiary.exe` when `RADIARY_EMBED_FFMPEG=ON`. The app extracts that embedded binary to a temp cache on first export and launches it from there.

If embedding is disabled, the build system copies `ffmpeg.exe` next to `Radiary.exe` instead.
