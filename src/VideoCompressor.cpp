#include "VideoCompressor.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>

static enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    (void)ctx;
    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }
    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

static int64_t fileSizeBytes(const std::string& path) {
    try {
        return static_cast<int64_t>(std::filesystem::file_size(std::filesystem::path(path)));
    } catch (...) {
        return -1;
    }
}

VideoCompressor::VideoCompressor(const std::string& input, const std::string& output,
    int interval, int kfInterval, int threshold, int dstW, int dstH, int inputW, int inputH, int quantize, bool cuda)
    : inputFile(input), outputFile(output), inputWidth(inputW), inputHeight(inputH),
    frameInterval(interval), keyframeInterval(kfInterval), changeThreshold(threshold),
    targetWidth(dstW), targetHeight(dstH), quantization(quantize), useCuda(cuda) {
    if (quantization < 0) quantization = 0;
    if (quantization > 4) quantization = 4;
}

VideoCompressor::~VideoCompressor() = default;

bool VideoCompressor::initialize() {
    hw_pix_fmt = AV_PIX_FMT_NONE;

    if (frameInterval < 1 || keyframeInterval < 1) {
        std::cerr << "Frame interval and keyframe interval must be >= 1." << std::endl;
        return false;
    }
    if (changeThreshold < 0 || changeThreshold > 255) {
        std::cerr << "Threshold must be in range 0-255." << std::endl;
        return false;
    }

    AVFormatContext* rawFmtCtx = nullptr;
    AVDictionary* options = nullptr;

    if (inputFile.size() >= 4 && inputFile.substr(inputFile.size() - 4) == ".yuv") {
        if (inputWidth <= 0 || inputHeight <= 0) {
            std::cerr << "[Error] Raw YUV input requires --in-width and --in-height." << std::endl;
            return false;
        }
        std::string sizeStr = std::to_string(inputWidth) + "x" + std::to_string(inputHeight);
        av_dict_set(&options, "video_size", sizeStr.c_str(), 0);
        av_dict_set(&options, "pixel_format", "yuv420p", 0);
        std::cout << "[Info] Opening raw YUV file: " << sizeStr << " @ yuv420p" << std::endl;
    }

    if (avformat_open_input(&rawFmtCtx, inputFile.c_str(), nullptr, &options) < 0) {
        std::cerr << "Could not open input file: " << inputFile << std::endl;
        if (options) av_dict_free(&options);
        return false;
    }
    if (options) av_dict_free(&options);
    fmtCtx.reset(rawFmtCtx);

    if (avformat_find_stream_info(fmtCtx.get(), nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return false;
    }

    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }

    if (videoStreamIdx == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        return false;
    }

    AVCodecParameters* codecParams = fmtCtx->streams[videoStreamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);

    if (!codec) {
        std::cerr << "Unsupported codec" << std::endl;
        return false;
    }

    codecCtx.reset(avcodec_alloc_context3(codec));
    if (!codecCtx) {
        std::cerr << "Could not allocate codec context" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx.get(), codecParams) < 0) {
        std::cerr << "Could not copy codec params to context" << std::endl;
        return false;
    }

    if (useCuda) {
        AVBufferRef* ctx = nullptr;
        int err = av_hwdevice_ctx_create(&ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        if (err < 0) {
            std::cerr << "Failed to create CUDA device. Error code: " << err << std::endl;
            std::cerr << "Falling back to software decoding." << std::endl;
            useCuda = false;
        } else {
            hwDeviceCtx.reset(ctx);
            codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx.get());
            hw_pix_fmt = AV_PIX_FMT_CUDA;
            codecCtx->get_format = get_hw_format;
            std::cout << "[Info] CUDA hardware decoding enabled." << std::endl;
        }
    }

    if (avcodec_open2(codecCtx.get(), codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return false;
    }

    metadata.width = (targetWidth > 0) ? targetWidth : codecCtx->width;
    metadata.height = (targetHeight > 0) ? targetHeight : codecCtx->height;
    metadata.frameInterval = frameInterval;
    metadata.keyframeInterval = keyframeInterval;
    metadata.changeThreshold = changeThreshold;
    metadata.quantization = quantization;
    metadata.duration = fmtCtx->duration;
    metadata.totalFrames = 0;

    if (metadata.width <= 0 || metadata.height <= 0 ||
        (metadata.width % 2) != 0 || (metadata.height % 2) != 0) {
        std::cerr << "Output dimensions must be positive even numbers for YUV420P (got "
                  << metadata.width << "x" << metadata.height << ")." << std::endl;
        return false;
    }

    ofs.open(outputFile, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << "Could not open output file: " << outputFile << std::endl;
        return false;
    }

    writeHeader();

    return true;
}

bool VideoCompressor::handleDecodedFrame(AVFrame* processingFrame, int& frameCount,
                                        AVFrame* scaledFrame) {
    frameCount++;

    if (!swsCtx ||
        currentSwsW != processingFrame->width ||
        currentSwsH != processingFrame->height ||
        currentSwsFormat != processingFrame->format) {
        SwsContext* ctx = sws_getContext(
            processingFrame->width, processingFrame->height, (enum AVPixelFormat)processingFrame->format,
            metadata.width, metadata.height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!ctx) {
            std::cerr << "sws_getContext failed for frame " << frameCount << std::endl;
            return false;
        }
        swsCtx.reset(ctx);
        currentSwsW = processingFrame->width;
        currentSwsH = processingFrame->height;
        currentSwsFormat = (enum AVPixelFormat)processingFrame->format;
    }

    int h = sws_scale(swsCtx.get(),
        processingFrame->data, processingFrame->linesize,
        0, processingFrame->height,
        scaledFrame->data, scaledFrame->linesize);

    if (h <= 0) {
        std::cerr << "sws_scale failed for frame " << frameCount << std::endl;
        return false;
    }

    int64_t pts = (processingFrame->pts != AV_NOPTS_VALUE) ? processingFrame->pts
                                                           : processingFrame->best_effort_timestamp;
    if (frameCount % frameInterval == 0) {
        processAndWriteFrame(scaledFrame, pts);
        if (savedFrameCount % 10 == 0) {
            std::cout << "Processed " << savedFrameCount << " frames (total decoded: " << frameCount << ")    \r" << std::flush;
        }
    }
    return true;
}

bool VideoCompressor::compress() {
    AVPacketPtr packet(av_packet_alloc());
    AVFramePtr frame(av_frame_alloc());
    AVFramePtr swFrame(av_frame_alloc());
    AVFramePtr scaledFrame(av_frame_alloc());

    if (!packet || !frame || !swFrame || !scaledFrame) {
        std::cerr << "Could not allocate packet/frame" << std::endl;
        return false;
    }

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, metadata.width, metadata.height, 1);
    if (numBytes <= 0) {
        std::cerr << "Invalid output dimensions." << std::endl;
        return false;
    }
    struct AvFreeDeleter { void operator()(void* p) const { av_free(p); } };
    std::unique_ptr<uint8_t, AvFreeDeleter> buffer((uint8_t*)av_malloc((size_t)numBytes));

    if (!buffer) {
        std::cerr << "Could not allocate image buffer" << std::endl;
        return false;
    }

    av_image_fill_arrays(scaledFrame->data, scaledFrame->linesize, buffer.get(),
        AV_PIX_FMT_YUV420P, metadata.width, metadata.height, 1);
    scaledFrame->width = metadata.width;
    scaledFrame->height = metadata.height;
    scaledFrame->format = AV_PIX_FMT_YUV420P;

    int frameCount = 0;

    std::cout << "Processing video..." << std::endl;

    auto drainFrames = [&](bool& ok) {
        while (true) {
            int ret = avcodec_receive_frame(codecCtx.get(), frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                std::cerr << "Error during decoding" << std::endl;
                ok = false;
                break;
            }
            AVFrame* processingFrame = frame.get();
            if (frame->format == hw_pix_fmt) {
                int err = av_hwframe_transfer_data(swFrame.get(), frame.get(), 0);
                if (err < 0) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(err, errbuf, AV_ERROR_MAX_STRING_SIZE);
                    std::cerr << "Error transferring data to system memory: " << errbuf << std::endl;
                    cudaErrorOccurred = true;
                    av_frame_unref(frame.get());
                    ok = false;
                    return;
                }
                swFrame->pts = frame->pts;
                swFrame->best_effort_timestamp = frame->best_effort_timestamp;
                processingFrame = swFrame.get();
            }
            handleDecodedFrame(processingFrame, frameCount, scaledFrame.get());
            av_frame_unref(frame.get());
            av_frame_unref(swFrame.get());
        }
    };

    bool streamOk = true;
    while (av_read_frame(fmtCtx.get(), packet.get()) >= 0) {
        if (packet->stream_index == videoStreamIdx) {
            int ret = avcodec_send_packet(codecCtx.get(), packet.get());
            if (ret < 0) {
                av_packet_unref(packet.get());
                continue;
            }
            drainFrames(streamOk);
            if (!streamOk && cudaErrorOccurred)
                return false;
        }
        av_packet_unref(packet.get());
    }

    avcodec_send_packet(codecCtx.get(), nullptr);
    drainFrames(streamOk);
    if (!streamOk && cudaErrorOccurred)
        return false;

    std::cout << std::endl;

    metadata.totalFrames = savedFrameCount;
    totalProcessedFrames = frameCount;

    finalizeFile();
    printStats();

    return true;
}

bool VideoCompressor::hasCudaError() const { return cudaErrorOccurred; }

void VideoCompressor::writeHeader() {
    std::vector<uint8_t> hdr = ige::encodeHeader(metadata);
    headerPos = ofs.tellp();
    ofs.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
}

void VideoCompressor::finalizeFile() {
    if (!ofs) return;
    metadata.totalFrames = savedFrameCount;
    ofs.seekp(headerPos);
    std::vector<uint8_t> hdr = ige::encodeHeader(metadata);
    ofs.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    ofs.close();
}

void VideoCompressor::processAndWriteFrame(AVFrame* frame, int64_t pts) {
    int w = frame->width;
    int h = frame->height;
    int uvw = w / 2;
    int uvh = h / 2;

    size_t ySize = static_cast<size_t>(w) * static_cast<size_t>(h);
    size_t uvSize = static_cast<size_t>(uvw) * static_cast<size_t>(uvh);
    size_t totalSize = ySize + 2 * uvSize;

    std::vector<uint8_t> currentFrame(totalSize);
    uint8_t* dst = currentFrame.data();

    for (int i = 0; i < h; i++) {
        memcpy(dst + static_cast<size_t>(i) * static_cast<size_t>(w),
               frame->data[0] + static_cast<size_t>(i) * static_cast<size_t>(frame->linesize[0]),
               static_cast<size_t>(w));
    }
    dst += ySize;

    for (int i = 0; i < uvh; i++) {
        memcpy(dst + static_cast<size_t>(i) * static_cast<size_t>(uvw),
               frame->data[1] + static_cast<size_t>(i) * static_cast<size_t>(frame->linesize[1]),
               static_cast<size_t>(uvw));
    }
    dst += uvSize;

    for (int i = 0; i < uvh; i++) {
        memcpy(dst + static_cast<size_t>(i) * static_cast<size_t>(uvw),
               frame->data[2] + static_cast<size_t>(i) * static_cast<size_t>(frame->linesize[2]),
               static_cast<size_t>(uvw));
    }

    uint8_t frameType = 0;
    std::vector<uint8_t> compressedData;
    bool shouldBeKeyframe = (savedFrameCount % keyframeInterval == 0);

    if (lastFrame.empty() || shouldBeKeyframe) {
        frameType = 1;
        compressedData = compressKeyframe(currentFrame);
        keyframeCount++;
        totalKeyframeBytes += compressedData.size();
    } else {
        std::vector<uint8_t> deltaData = compressDelta(currentFrame, lastFrame);
        if (deltaData.size() < currentFrame.size()) {
            frameType = 0;
            compressedData = std::move(deltaData);
            totalDeltaBytes += compressedData.size();
        } else {
            std::vector<uint8_t> keyframeData = compressKeyframe(currentFrame);
            if (deltaData.size() < keyframeData.size()) {
                frameType = 0;
                compressedData = std::move(deltaData);
                totalDeltaBytes += compressedData.size();
            } else {
                frameType = 1;
                totalKeyframeBytes += keyframeData.size();
                compressedData = std::move(keyframeData);
                keyframeCount++;
            }
        }
    }

    writeFrameToDisk(pts, frameType, compressedData);
    lastFrame = std::move(currentFrame);
    savedFrameCount++;
}

void VideoCompressor::writeFrameToDisk(int64_t pts, uint8_t frameType, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> pkt;
    pkt.reserve(8 + 1 + 4 + data.size());
    ige::putI64LE(pkt, pts);
    pkt.push_back(frameType);
    ige::putU32LE(pkt, static_cast<uint32_t>(data.size()));
    pkt.insert(pkt.end(), data.begin(), data.end());
    ofs.write(reinterpret_cast<const char*>(pkt.data()), static_cast<std::streamsize>(pkt.size()));
}

std::vector<uint8_t> VideoCompressor::compressKeyframe(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> compressed;
    compressed.reserve(data.size() / 2);

    uint8_t mask = static_cast<uint8_t>(0xFF << quantization);

    size_t i = 0;
    while (i < data.size()) {
        size_t runLen = 1;
        uint8_t val = static_cast<uint8_t>(data[i] & mask);

        while (i + runLen < data.size() && (data[i + runLen] & mask) == val && runLen < 255) {
            runLen++;
        }

        if (runLen >= 4) {
            compressed.push_back(0xFF);
            compressed.push_back(static_cast<uint8_t>(runLen));
            compressed.push_back(val);
            i += runLen;
        } else {
            size_t literalStart = i;
            size_t literalLen = 0;
            while (i < data.size() && literalLen < 127) {
                size_t nextRun = 1;
                uint8_t currentVal = static_cast<uint8_t>(data[i] & mask);
                if (i + 1 < data.size()) {
                    while (i + nextRun < data.size() && (data[i + nextRun] & mask) == currentVal && nextRun < 4) nextRun++;
                }
                if (nextRun >= 4) break;
                literalLen++;
                i++;
            }
            compressed.push_back(static_cast<uint8_t>(literalLen));
            for (size_t j = 0; j < literalLen; j++)
                compressed.push_back(static_cast<uint8_t>(data[literalStart + j] & mask));
        }
    }
    return compressed;
}

std::vector<uint8_t> VideoCompressor::compressDelta(const std::vector<uint8_t>& current, const std::vector<uint8_t>& previous) {
    std::vector<uint8_t> compressed;
    compressed.reserve(current.size() / 10);

    uint8_t mask = static_cast<uint8_t>(0xFF << quantization);

    size_t i = 0;
    size_t frameSize = current.size();

    while (i < frameSize) {
        while (i < frameSize &&
            std::abs(static_cast<int>(current[i] & mask) - static_cast<int>(previous[i] & mask)) < changeThreshold) {
            i++;
        }

        if (i >= frameSize) break;

        size_t changeStart = i;
        std::vector<int8_t> deltas;
        deltas.reserve(64);

        while (i < frameSize && deltas.size() < 255) {
            int diff = static_cast<int>(current[i] & mask) - static_cast<int>(previous[i] & mask);
            if (std::abs(diff) >= changeThreshold) {
                int8_t delta = (diff > 127) ? 127 : (diff < -128) ? -128 : static_cast<int8_t>(diff);
                deltas.push_back(delta);
                i++;
            } else {
                if (deltas.size() >= 3) break;
                if (i - changeStart < 5) {
                    deltas.push_back(0);
                    i++;
                } else break;
            }
        }

        if (deltas.empty()) continue;

        writeVarint(compressed, changeStart);
        compressed.push_back(static_cast<uint8_t>(deltas.size()));
        for (int8_t delta : deltas) compressed.push_back(static_cast<uint8_t>(delta));
    }
    return compressed;
}

void VideoCompressor::writeVarint(std::vector<uint8_t>& out, size_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void VideoCompressor::printStats() {
    std::cout << "\n=== COMPRESSION DIAGNOSTICS ===" << std::endl;
    std::cout << "Total frames: " << savedFrameCount << std::endl;
    std::cout << "Key frames: " << keyframeCount << " ("
        << totalKeyframeBytes / (1024.0 * 1024.0) << " MB)" << std::endl;
    size_t deltaFrames = (savedFrameCount >= static_cast<int>(keyframeCount))
        ? static_cast<size_t>(savedFrameCount) - keyframeCount : 0;
    std::cout << "Delta frames: " << deltaFrames
        << " (" << totalDeltaBytes / (1024.0 * 1024.0) << " MB)" << std::endl;

    std::cout << "\nCompressed video saved to: " << outputFile << std::endl;

    uint64_t pixels = static_cast<uint64_t>(metadata.width) * static_cast<uint64_t>(metadata.height);
    uint64_t originalSize = (pixels * 3 * static_cast<uint64_t>(savedFrameCount)) / 2;

    int64_t compressedBytes = fileSizeBytes(outputFile);
    int64_t inputBytes = fileSizeBytes(inputFile);
    double compressedMB = compressedBytes >= 0 ? compressedBytes / (1024.0 * 1024.0) : -1.0;
    double inputMB = inputBytes >= 0 ? inputBytes / (1024.0 * 1024.0) : -1.0;

    double ratio = (compressedBytes > 0) ?
        static_cast<double>(originalSize) / static_cast<double>(compressedBytes) : 0.0;
    double vsMP4 = (inputBytes > 0 && compressedBytes > 0) ?
        static_cast<double>(inputBytes) / static_cast<double>(compressedBytes) : 0.0;

    std::cout << "\n=== COMPRESSION RESULTS ===" << std::endl;
    std::cout << "Input MP4 file size: " << inputMB << " MB" << std::endl;
    std::cout << "Output .ige file size: " << compressedMB << " MB" << std::endl;
    std::cout << "Compression vs MP4: " << vsMP4 << ":1 ";

    if (vsMP4 < 1.0) {
        std::cout << "(output larger than MP4; expected since MP4 is entropy-coded)" << std::endl;
        std::cout << "\nSuggestions:" << std::endl;
        std::cout << "  - Increase threshold (current: " << changeThreshold << ")" << std::endl;
        std::cout << "  - Increase frame interval (current: " << frameInterval << ")" << std::endl;
        std::cout << "  - Reduce keyframe frequency (current: every " << keyframeInterval << " frames)" << std::endl;
    } else {
        std::cout << "(smaller than input)" << std::endl;
    }

    std::cout << "\nVideo resolution: " << metadata.width << "x" << metadata.height << "px" << std::endl;
    std::cout << "Raw sampled size: " << originalSize / (1024 * 1024) << " MB" << std::endl;
    std::cout << "Sampled frames saved: " << metadata.totalFrames << std::endl;
    std::cout << "Compression ratio (vs raw): " << ratio << ":1" << std::endl;
}
