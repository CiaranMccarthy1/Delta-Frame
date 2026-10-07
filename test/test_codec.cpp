#include <cassert>
#include <iostream>
#include <vector>
#include "../src/IgeFormat.h"

static void testVarint() {
    const size_t vals[] = {0, 1, 127, 128, 300, 16384, 1000000};
    for (size_t v : vals) {
        std::vector<uint8_t> buf;
        size_t t = v;
        while (t >= 0x80) {
            buf.push_back(static_cast<uint8_t>((t & 0x7F) | 0x80));
            t >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(t));
        size_t off = 0, got = 0;
        assert(ige::readVarint(buf.data(), buf.size(), off, got));
        assert(got == v && off == buf.size());
    }
}

static void testHeader() {
    ige::Header h{};
    h.width = 640;
    h.height = 360;
    h.frameInterval = 10;
    h.duration = 123456;
    h.totalFrames = 42;
    h.keyframeInterval = 30;
    h.changeThreshold = 15;
    h.quantization = 2;
    auto enc = ige::encodeHeader(h);
    assert(enc.size() == ige::kHeaderSize);
    ige::Header d{};
    assert(ige::decodeHeader(enc.data(), enc.size(), d));
    assert(d.width == 640 && d.height == 360 && d.totalFrames == 42);
    assert(d.quantization == 2 && d.keyframeInterval == 30);
    enc[0] = 'X';
    assert(!ige::decodeHeader(enc.data(), enc.size(), d));
}

static void testKeyframe() {
    std::vector<uint8_t> raw(1000, 7);
    for (size_t i = 200; i < 210; i++) raw[i] = static_cast<uint8_t>(i);
    for (size_t i = 500; i < 600; i++) raw[i] = 9;

    std::vector<uint8_t> comp;
    comp.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        size_t run = 1;
        while (i + run < raw.size() && raw[i + run] == raw[i] && run < 255) run++;
        if (run >= 4) {
            comp.push_back(0xFF);
            comp.push_back(static_cast<uint8_t>(run));
            comp.push_back(raw[i]);
            i += run;
        } else {
            size_t start = i, n = 0;
            while (i < raw.size() && n < 127) {
                size_t nr = 1;
                while (i + nr < raw.size() && raw[i + nr] == raw[i] && nr < 4) nr++;
                if (nr >= 4) break;
                n++;
                i++;
            }
            comp.push_back(static_cast<uint8_t>(n));
            for (size_t j = 0; j < n; j++) comp.push_back(raw[start + j]);
        }
    }
    std::vector<uint8_t> out;
    assert(ige::decodeKeyframe(comp, out, raw.size()));
    assert(out == raw);
}

static void testDelta() {
    std::vector<uint8_t> prev(64, 10);
    std::vector<uint8_t> cur = prev;
    cur[5] = 50;
    cur[6] = 55;
    cur[30] = 100;
    std::vector<uint8_t> payload;
    auto putVar = [&](size_t v) {
        while (v >= 0x80) {
            payload.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        payload.push_back(static_cast<uint8_t>(v));
    };
    putVar(5);
    payload.push_back(2);
    payload.push_back(static_cast<uint8_t>(int8_t(40)));
    payload.push_back(static_cast<uint8_t>(int8_t(45)));
    putVar(30);
    payload.push_back(1);
    payload.push_back(static_cast<uint8_t>(int8_t(90)));
    std::vector<uint8_t> out;
    assert(ige::applyDelta(prev, payload, out));
    assert(out == cur);
    std::vector<uint8_t> empty;
    assert(ige::applyDelta(prev, empty, out));
    assert(out == prev);
}

int main() {
    testVarint();
    testHeader();
    testKeyframe();
    testDelta();
    std::cout << "All codec tests passed." << std::endl;
    return 0;
}
