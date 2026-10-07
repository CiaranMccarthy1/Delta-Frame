# Delta-Encoded Video Codec

A C++ video compression engine using temporal delta encoding and Run-Length Encoding (RLE) for low-entropy footage (screen recordings, presentations, static-camera feeds).

## What It Does

- **Temporal Delta Encoding**: Stores only pixels that change between frames.
- **Hardware-assisted decoding**: Optional CUDA HW decode (`--cuda`) with automatic CPU fallback. Differencing and RLE run on CPU; decode path stays CPU-light.
- **RLE Keyframes + Sparse Deltas**: Keyframes use RLE; inter frames store `(position varint, length, signed deltas)`.
- **Keyframe Safety**: Periodic keyframes bound error propagation; per-frame fallback picks the smaller of delta vs keyframe when beneficial.

## How It Works

```
Input Frame
    │
    ▼
Spatial Downscale ──► Bit-Quantization ──► Threshold Filter
    │
    ▼
Compare vs Previous Frame (absolute diff >= threshold)
    │
    ├──► Unchanged ──► Skipped (zero bytes)
    │
    └──► Changed ──► Delta Packet (varint position + deltas)
```

Sample every Nth decoded frame (`--interval`), emit a keyframe every M saved frames (`--keyframe`).

## Build

### Linux

**Ubuntu/Debian:**
```bash
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev pkg-config cmake build-essential
```

**Arch Linux:**
```bash
sudo pacman -S ffmpeg pkgconf cmake base-devel
```

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build
```

Binaries: `build/video-compression`, `build/ige-decode`, `build/test-codec`.

### Windows
Install FFmpeg to `C:/ffmpeg` (or edit `CMakeLists.txt`) and CMake:
```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Usage

```bash
./build/video-compression input.mp4 output.ige \
    --width 640 --height 360 \
    --quantize 2 \
    --threshold 15 \
    --interval 10 \
    --keyframe 30

./build/ige-decode output.ige decoded.yuv
```

### Flags

| Flag | Description |
|------|-------------|
| `--cuda` | Try CUDA HW decoding, fall back to software |
| `--width`, `--height` | Target resolution; must be given together, both even (YUV420P) |
| `--in-width`, `--in-height` | Dimensions for raw `.yuv` input |
| `--quantize` | Bit-shift noise reduction (0–4, default 0) |
| `--interval` | Save every Nth decoded frame (default 10, >=1) |
| `--keyframe` | Keyframe every N saved frames (default 30, >=1) |
| `--threshold` | Absolute diff to register a change, 0–255 (default 15) |


## IGE — Interframe Granular Encoding
### Format 

```
magic:  "IGEDLT2" + 0x02
header: i32 width, i32 height, i32 frameInterval, i64 duration,
        i32 totalFrames, i32 keyframeInterval, i32 changeThreshold, i32 quantization
frames: repeat totalFrames times:
        i64 pts, u8 type (0=delta, 1=keyframe), u32 size, u8[size] payload
```

Keyframe payload: `0xFF len val` runs (len 1–255, runs >=4) or `len bytes` literals (len 0–127).
Delta payload: repeated `varint position, u8 count, i8[count] deltas`; empty payload means no change. Decoded byte is clamped `prev + delta` to 0–255. Diffs beyond ±127 saturate and are lossy.

## Limitations

- Output is usually larger than H.264 MP4 (expected); the win is decode simplicity vs raw YUV, not vs H.264 bitrate.
- Lossy when quantization > 0, threshold > 0, or diffs exceed int8 range.
- YUV420P only; dimensions must be even.

## License

MIT
