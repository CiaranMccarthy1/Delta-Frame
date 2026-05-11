# GPU-Accelerated Delta-Encoded Video Codec

A high-throughput C++ video compression engine utilizing temporal delta encoding and Run-Length Encoding (RLE) to achieve extreme compression ratios on low-entropy footage.

### What makes this interesting

The implementation leverages a custom SIMD-friendly bit-packing format that treats video as a stream of sparse pixel updates, minimizing I/O bottlenecks in static scenes. By offloading frame differencing and quantization to CUDA kernels, the codec achieves massive parallelization during the encoding bottleneck. This architecture prioritizes O(1) decode complexity per pixel change, making it a viable candidate for resource-constrained embedded playback where H.264 entropy decoding is computationally prohibitive.

### Architecture overview

The codec operates on a keyframe-plus-delta model to mitigate error propagation while maximizing temporal redundancy. Incoming frames are processed through a CUDA-accelerated pipeline that performs spatial downscaling and bit-quantization to filter sensor noise before calculating the per-channel Manhattan distance between sequential frames. Changes exceeding a configurable bit-threshold are serialized via an RLE-compressed stream, while static regions are represented as zero-length skip-instructions. This design intentionally trades off inter-frame prediction complexity for raw throughput and deterministic decoding logic. [VERIFY: The implementation utilizes a custom binary serialization format for the RLE-encoded stream.]

### Usage

The following example demonstrates a high-efficiency archival configuration using CUDA-accelerated quantization and aggressive temporal filtering:

```powershell
./video-compression.exe input.mp4 output.ige --cuda --width 640 --height 360 --quantize 3 --threshold 40 --interval 10
