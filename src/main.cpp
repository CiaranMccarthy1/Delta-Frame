#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include "VideoCompressor.h"

void printHelp(const char* progName) {
    std::cout << "Video Delta Compressor (YUV420P Streaming)" << std::endl;
    std::cout << "Usage: " << progName << " <input.mp4> <output.ige> [OPTIONS]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --cuda             Enable CUDA hardware decoding (NVIDIA only, falls back to CPU)" << std::endl;
    std::cout << "  --width <W>        Target width, requires --height (must be positive even)" << std::endl;
    std::cout << "  --height <H>       Target height, requires --width (must be positive even)" << std::endl;
    std::cout << "  --in-width <W>     Input width (required for raw .yuv)" << std::endl;
    std::cout << "  --in-height <H>    Input height (required for raw .yuv)" << std::endl;
    std::cout << "  --quantize <0-4>   Bit-shift quantization for noise reduction (default: 0)" << std::endl;
    std::cout << "  --interval <N>     Save every Nth decoded frame (default: 10, >=1)" << std::endl;
    std::cout << "  --keyframe <N>     Insert a keyframe every N saved frames (default: 30, >=1)" << std::endl;
    std::cout << "  --threshold <T>    Pixel change threshold (0-255) (default: 15)" << std::endl;
    std::cout << "  --help             Show this help message" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  " << progName << " input.mp4 output.ige --width 640 --height 360 --quantize 2" << std::endl;
}

static bool parseInt(const char* s, int& out) {
    try {
        size_t pos = 0;
        int v = std::stoi(s, &pos);
        if (s[pos] != '\0') return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

int main(int argc, char* argv[]) {
    bool useCuda = false;
    std::vector<std::string> args;
    int targetWidth = 0;
    int targetHeight = 0;
    int inputWidth = 0;
    int inputHeight = 0;
    int quantization = 0;
    int frameInterval = 10;
    int keyframeInterval = 30;
    int changeThreshold = 15;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto needValue = [&](int& dst) {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value." << std::endl;
                return false;
            }
            if (!parseInt(argv[++i], dst)) {
                std::cerr << "Error: invalid integer for " << arg << ": " << argv[i] << std::endl;
                return false;
            }
            return true;
        };
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "--cuda") {
            useCuda = true;
        } else if (arg == "--width") {
            if (!needValue(targetWidth)) return 1;
        } else if (arg == "--height") {
            if (!needValue(targetHeight)) return 1;
        } else if (arg == "--in-width") {
            if (!needValue(inputWidth)) return 1;
        } else if (arg == "--in-height") {
            if (!needValue(inputHeight)) return 1;
        } else if (arg == "--quantize") {
            if (!needValue(quantization)) return 1;
        } else if (arg == "--interval") {
            if (!needValue(frameInterval)) return 1;
        } else if (arg == "--keyframe") {
            if (!needValue(keyframeInterval)) return 1;
        } else if (arg == "--threshold") {
            if (!needValue(changeThreshold)) return 1;
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Error: unknown option " << arg << std::endl;
            printHelp(argv[0]);
            return 1;
        } else {
            args.push_back(arg);
        }
    }

    if (args.size() < 2) {
        std::cout << "Error: Missing input/output file arguments." << std::endl;
        printHelp(argv[0]);
        return 1;
    }
    if (args.size() > 2) {
        std::cerr << "Error: too many positional arguments. Use --interval/--keyframe/--threshold flags." << std::endl;
        return 1;
    }

    if (frameInterval < 1) {
        std::cerr << "Error: --interval must be >= 1." << std::endl;
        return 1;
    }
    if (keyframeInterval < 1) {
        std::cerr << "Error: --keyframe must be >= 1." << std::endl;
        return 1;
    }
    if (changeThreshold < 0 || changeThreshold > 255) {
        std::cerr << "Error: --threshold must be 0-255." << std::endl;
        return 1;
    }
    if (quantization < 0 || quantization > 4) {
        std::cerr << "Error: --quantize must be 0-4." << std::endl;
        return 1;
    }
    if ((targetWidth > 0) != (targetHeight > 0)) {
        std::cerr << "Error: --width and --height must be given together." << std::endl;
        return 1;
    }
    if (targetWidth < 0 || targetHeight < 0) {
        std::cerr << "Error: --width/--height must be positive." << std::endl;
        return 1;
    }
    if (targetWidth > 0 && ((targetWidth % 2) != 0 || (targetHeight % 2) != 0)) {
        std::cerr << "Error: --width/--height must be even for YUV420P." << std::endl;
        return 1;
    }
    if ((inputWidth > 0) != (inputHeight > 0)) {
        std::cerr << "Error: --in-width and --in-height must be given together." << std::endl;
        return 1;
    }

    std::string inputFile = args[0];
    std::string outputFile = args[1];

    std::cout << "Video Delta Compressor" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Input: " << inputFile << std::endl;
    std::cout << "Output: " << outputFile << std::endl;
    std::cout << "Frame interval: " << frameInterval << std::endl;
    std::cout << "Keyframe interval: " << keyframeInterval << std::endl;
    std::cout << "Change threshold: " << changeThreshold << std::endl;
    std::cout << "HW Acceleration: " << (useCuda ? "CUDA" : "None") << std::endl;
    if (targetWidth > 0 && targetHeight > 0) {
        std::cout << "Target Resolution: " << targetWidth << "x" << targetHeight << std::endl;
    }
    std::cout << "Quantization: " << quantization << " bits" << std::endl << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();

    while (true) {
        VideoCompressor compressor(inputFile, outputFile, frameInterval, keyframeInterval, changeThreshold,
            targetWidth, targetHeight, inputWidth, inputHeight, quantization, useCuda);

        if (!compressor.initialize()) {
            std::cerr << "Failed to initialize compressor" << std::endl;
            if (useCuda) {
                std::cout << "Retrying with Software Decoding..." << std::endl;
                useCuda = false;
                continue;
            }
            return 1;
        }

        if (!compressor.compress()) {
            std::cerr << "Compression failed" << std::endl;
            if (useCuda && compressor.hasCudaError()) {
                std::cout << "CUDA Error detected. Retrying with Software Decoding..." << std::endl;
                useCuda = false;
                continue;
            }
            return 1;
        }
        break;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = endTime - startTime;
    long long micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

    if (micros < 1000) std::cout << "\nTime taken: " << micros << " us" << std::endl;
    else if (micros < 1'000'000) std::cout << "\nTime taken: " << micros / 1000.0 << " ms" << std::endl;
    else std::cout << "\nTime taken: " << micros / 1'000'000.0 << " s" << std::endl;

    std::cout << "\nCompression completed successfully!" << std::endl;

    return 0;
}
