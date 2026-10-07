#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include "FFmpegUtils.h"
#include "IgeFormat.h"

class VideoCompressor {
private:
    std::string inputFile;
    std::string outputFile;
    int inputWidth;
    int inputHeight;
    int frameInterval;
    int keyframeInterval;
    int changeThreshold;
    int targetWidth;
    int targetHeight;
    int quantization;
    bool useCuda;

    int totalProcessedFrames = 0;
    int savedFrameCount = 0;

    size_t keyframeCount = 0;
    size_t totalKeyframeBytes = 0;
    size_t totalDeltaBytes = 0;

    AVFormatContextPtr fmtCtx;
    AVCodecContextPtr codecCtx;
    SwsContextPtr swsCtx;
    AVBufferRefPtr hwDeviceCtx;
    int videoStreamIdx = -1;

    ige::Header metadata{};
    std::vector<uint8_t> lastFrame;
    std::ofstream ofs;
    std::streampos headerPos;

    int currentSwsW = -1;
    int currentSwsH = -1;
    enum AVPixelFormat currentSwsFormat = AV_PIX_FMT_NONE;

    bool cudaErrorOccurred = false;

public:
    VideoCompressor(const std::string& input, const std::string& output,
        int interval, int kfInterval, int threshold,
        int targetW, int targetH, int inputW, int inputH, int quantize, bool cuda);

    ~VideoCompressor();

    bool initialize();
    bool compress();
    bool hasCudaError() const;

private:
    void writeHeader();
    void finalizeFile();
    bool handleDecodedFrame(AVFrame* processingFrame, int& frameCount, AVFrame* scaledFrame);
    void processAndWriteFrame(AVFrame* frame, int64_t pts);
    void writeFrameToDisk(int64_t pts, uint8_t frameType, const std::vector<uint8_t>& data);
    std::vector<uint8_t> compressKeyframe(const std::vector<uint8_t>& data);
    std::vector<uint8_t> compressDelta(const std::vector<uint8_t>& current, const std::vector<uint8_t>& previous);
    void writeVarint(std::vector<uint8_t>& out, size_t value);
    void printStats();
};
