#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include "IgeFormat.h"

static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    ifs.seekg(0, std::ios::end);
    std::streamsize n = ifs.tellg();
    if (n < 0) return false;
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(n));
    if (n > 0 && !ifs.read(reinterpret_cast<char*>(out.data()), n)) return false;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <input.ige> [output.yuv]" << std::endl;
        return 1;
    }
    std::vector<uint8_t> buf;
    if (!readAll(argv[1], buf)) {
        std::cerr << "Could not read input file." << std::endl;
        return 1;
    }
    ige::Header h{};
    if (!ige::decodeHeader(buf.data(), buf.size(), h)) {
        std::cerr << "Invalid .ige header (bad magic/version)." << std::endl;
        return 1;
    }
    if (h.width <= 0 || h.height <= 0 || (h.width % 2) != 0 || (h.height % 2) != 0) {
        std::cerr << "Invalid dimensions in header." << std::endl;
        return 1;
    }
    size_t frameSize = static_cast<size_t>(h.width) * static_cast<size_t>(h.height) * 3 / 2;
    size_t off = ige::kHeaderSize;
    std::vector<uint8_t> prev(frameSize, 0);
    std::vector<uint8_t> cur;
    std::vector<uint8_t> decoded;
    std::ofstream ofs;
    if (argc == 3) {
        ofs.open(argv[2], std::ios::binary | std::ios::trunc);
        if (!ofs) {
            std::cerr << "Could not open output file." << std::endl;
            return 1;
        }
    }
    int count = 0;
    bool first = true;
    while (off < buf.size()) {
        if (off + 8 + 1 + 4 > buf.size()) {
            std::cerr << "Truncated frame packet at frame " << count << "." << std::endl;
            return 1;
        }
        size_t p = off;
        int64_t pts = 0;
        for (int i = 0; i < 8; i++) pts |= static_cast<int64_t>(buf[p + i]) << (8 * i);
        uint8_t type = buf[p + 8];
        uint32_t size = static_cast<uint32_t>(buf[p + 9]) |
                        (static_cast<uint32_t>(buf[p + 10]) << 8) |
                        (static_cast<uint32_t>(buf[p + 11]) << 16) |
                        (static_cast<uint32_t>(buf[p + 12]) << 24);
        off += 8 + 1 + 4;
        if (off + size > buf.size()) {
            std::cerr << "Truncated frame payload at frame " << count << "." << std::endl;
            return 1;
        }
        std::vector<uint8_t> payload(buf.begin() + off, buf.begin() + off + size);
        off += size;
        (void)pts;
        if (type == 1) {
            if (!ige::decodeKeyframe(payload, cur, frameSize)) {
                std::cerr << "Keyframe decode failed at frame " << count << "." << std::endl;
                return 1;
            }
        } else if (type == 0) {
            if (first) {
                std::cerr << "First frame must be a keyframe." << std::endl;
                return 1;
            }
            if (!ige::applyDelta(prev, payload, cur)) {
                std::cerr << "Delta decode failed at frame " << count << "." << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown frame type at frame " << count << "." << std::endl;
            return 1;
        }
        if (cur.size() != frameSize) {
            std::cerr << "Decoded size mismatch at frame " << count << "." << std::endl;
            return 1;
        }
        if (ofs) ofs.write(reinterpret_cast<const char*>(cur.data()),
                           static_cast<std::streamsize>(cur.size()));
        prev = cur;
        first = false;
        count++;
    }
    if (count != h.totalFrames) {
        std::cerr << "Warning: header claims " << h.totalFrames << " frames, decoded " << count << "." << std::endl;
    }
    std::cout << "Decoded " << count << " frames (" << h.width << "x" << h.height << ")." << std::endl;
    return 0;
}
