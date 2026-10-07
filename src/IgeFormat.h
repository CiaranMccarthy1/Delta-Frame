#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace ige {

inline constexpr char kMagic[7] = {'I', 'G', 'E', 'D', 'L', 'T', '2'};
inline constexpr uint8_t kVersion = 2;
inline constexpr size_t kHeaderSize = 8 + 4 * 7 + 8;

struct Header {
    int32_t width = 0;
    int32_t height = 0;
    int32_t frameInterval = 10;
    int64_t duration = 0;
    int32_t totalFrames = 0;
    int32_t keyframeInterval = 30;
    int32_t changeThreshold = 15;
    int32_t quantization = 0;
};

inline void putU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void putI32LE(std::vector<uint8_t>& out, int32_t v) {
    putU32LE(out, static_cast<uint32_t>(v));
}

inline void putI64LE(std::vector<uint8_t>& out, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; i++)
        out.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xFF));
}

inline bool getU32LE(const uint8_t* p, size_t len, size_t& off, uint32_t& v) {
    if (off + 4 > len) return false;
    v = static_cast<uint32_t>(p[off]) | (static_cast<uint32_t>(p[off + 1]) << 8) |
        (static_cast<uint32_t>(p[off + 2]) << 16) | (static_cast<uint32_t>(p[off + 3]) << 24);
    off += 4;
    return true;
}

inline bool getI32LE(const uint8_t* p, size_t len, size_t& off, int32_t& v) {
    uint32_t u = 0;
    if (!getU32LE(p, len, off, u)) return false;
    v = static_cast<int32_t>(u);
    return true;
}

inline bool getI64LE(const uint8_t* p, size_t len, size_t& off, int64_t& v) {
    if (off + 8 > len) return false;
    uint64_t u = 0;
    for (int i = 0; i < 8; i++) u |= static_cast<uint64_t>(p[off + i]) << (8 * i);
    off += 8;
    v = static_cast<int64_t>(u);
    return true;
}

inline std::vector<uint8_t> encodeHeader(const Header& h) {
    std::vector<uint8_t> out;
    out.reserve(kHeaderSize);
    for (char c : kMagic) out.push_back(static_cast<uint8_t>(c));
    out.push_back(kVersion);
    putI32LE(out, h.width);
    putI32LE(out, h.height);
    putI32LE(out, h.frameInterval);
    putI64LE(out, h.duration);
    putI32LE(out, h.totalFrames);
    putI32LE(out, h.keyframeInterval);
    putI32LE(out, h.changeThreshold);
    putI32LE(out, h.quantization);
    return out;
}

inline bool decodeHeader(const uint8_t* p, size_t len, Header& h) {
    if (len < kHeaderSize) return false;
    for (int i = 0; i < 7; i++)
        if (p[i] != static_cast<uint8_t>(kMagic[i])) return false;
    if (p[7] != kVersion) return false;
    size_t off = 8;
    return getI32LE(p, len, off, h.width) && getI32LE(p, len, off, h.height) &&
           getI32LE(p, len, off, h.frameInterval) && getI64LE(p, len, off, h.duration) &&
           getI32LE(p, len, off, h.totalFrames) && getI32LE(p, len, off, h.keyframeInterval) &&
           getI32LE(p, len, off, h.changeThreshold) && getI32LE(p, len, off, h.quantization);
}

inline bool readVarint(const uint8_t* p, size_t len, size_t& off, size_t& v) {
    v = 0;
    int shift = 0;
    while (off < len) {
        uint8_t b = p[off++];
        if (shift >= 35) return false;
        v |= static_cast<size_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

inline bool decodeKeyframe(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
                           size_t expectedSize = 0) {
    out.clear();
    if (expectedSize) out.reserve(expectedSize);
    size_t i = 0;
    while (i < in.size()) {
        uint8_t b = in[i++];
        if (b == 0xFF) {
            if (i + 2 > in.size()) return false;
            size_t n = in[i++];
            uint8_t val = in[i++];
            if (n == 0) return false;
            out.insert(out.end(), n, val);
        } else {
            size_t n = b;
            if (n > 127 || i + n > in.size()) return false;
            out.insert(out.end(), in.begin() + i, in.begin() + i + n);
            i += n;
        }
    }
    if (expectedSize && out.size() != expectedSize) return false;
    return true;
}

inline bool applyDelta(const std::vector<uint8_t>& prev, const std::vector<uint8_t>& in,
                       std::vector<uint8_t>& out) {
    out = prev;
    size_t off = 0;
    while (off < in.size()) {
        size_t pos = 0;
        if (!readVarint(in.data(), in.size(), off, pos)) return false;
        if (off >= in.size()) return false;
        size_t n = in[off++];
        if (n == 0 || off + n > in.size()) return false;
        if (pos + n > out.size()) return false;
        for (size_t k = 0; k < n; k++) {
            int8_t d = 0;
            std::memcpy(&d, &in[off + k], 1);
            int v = static_cast<int>(out[pos + k]) + static_cast<int>(d);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            out[pos + k] = static_cast<uint8_t>(v);
        }
        off += n;
    }
    return true;
}

}
