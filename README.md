# GPU-Accelerated Delta-Encoded Video Codec

A high-throughput C++ video compression engine using temporal delta encoding and Run-Length Encoding (RLE) to achieve high compression ratios on low-entropy footage (e.g., screen recordings, presentations, static-camera feeds).

## What It Does

- **Temporal Delta Encoding**: Stores only the pixels that change between frames, rather than full frames.
- **CUDA-Accelerated Pipeline**: Frame differencing and quantization run on the GPU to maximize throughput.
- **RLE Serialization**: Changed regions are compressed via run-length encoding into a sparse update stream.
- **O(1) Decode**: Each pixel change decodes in constant time - no entropy tables or complex prediction logic.
- **Keyframe Safety**: Periodic keyframes prevent error propagation across long sequences.

## Why This Architecture

| Approach | Trade-off |
|----------|-----------|
| Delta-only encoding | High compression for static scenes, but drift risk without keyframes |
| CUDA for differencing | Massive parallelism on the encoder, but decode stays CPU-light |
| RLE instead of entropy coding | Slightly lower compression ratio, but deterministic decode time per pixel |
| Skip-instructions for static regions | Near-zero cost for unchanged areas |

This targets embedded or resource-constrained playback where H.264 entropy decoding is too expensive, not bitrate-optimized streaming.

## How It Works

```
Input Frame
    │
    ▼
Spatial Downscale ──► Bit-Quantization ──► Filter Noise
    │
    ▼
Manhattan Distance vs Previous Frame
    │
    ├──► Below threshold ──► Skip Instruction (zero bytes)
    │
    └──► Above threshold ──► RLE-Encoded Delta Block
```

## Build & Usage

```powershell
# Encode with CUDA acceleration, aggressive quantization, and 10-frame keyframe interval
./video-compression.exe input.mp4 output.ige `
    --cuda `
    --width 640 --height 360 `
    --quantize 3 `
    --threshold 40 `
    --interval 10
```

### Flags

| Flag | Description |
|------|-------------|
| `--cuda` | Enable GPU-accelerated differencing and quantization |
| `--width`, `--height` | Target resolution (downscales if larger) |
| `--quantize` | Bit-depth reduction for noise filtering (1–8) |
| `--threshold` | Manhattan distance threshold for triggering a delta block |
| `--interval` | Keyframe interval in frames |



## License

MIT
