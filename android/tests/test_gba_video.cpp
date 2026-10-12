#include "../app/src/main/cpp/gba_video.h"
#include "../app/src/main/cpp/frame_clock.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using wl4::GbaVideo;
using wl4::VideoState;

static void put16(uint8_t* memory, size_t offset, uint16_t val) {
    memory[offset] = val & 0xffu;
    memory[offset + 1] = val >> 8;
}
static uint32_t pixel(const std::vector<uint32_t>& rgba, int x, int y) {
    return rgba[y * VideoState::Width + x];
}
static constexpr uint32_t RED = 0xff0000ffu, GREEN = 0xff00ff00u;
static constexpr uint32_t BLUE = 0xffff0000u, BLACK = 0xff000000u;

static void testBackdropAndForcedBlank() {
    VideoState s;
    put16(s.palette.data(), 0, 0x001f);
    GbaVideo v;
    std::vector<uint32_t> out;
    v.Render(s, &out);
    assert(out.size() == 240 * 160);
    assert(pixel(out, 12, 29) == RED);
    s.dispcnt = 0x80;
    v.Render(s, &out);
    assert(pixel(out, 12, 29) == 0xffffffff);
}

static void testBg4bppScrollFlipAndTransparency() {
    VideoState s;
    // BG0: char base 0, screen base 8 (0x4000), priority 0.
    s.dispcnt = 0x100;
    s.bgcnt[0] = (8u << 8);
    put16(s.palette.data(), 2, 0x001f); // BG palette 1 red
    put16(s.palette.data(), 4, 0x03e0); // palette 2 green
    put16(s.palette.data(), 0, 0x7c00); // backdrop blue
    s.vram[0] = 0x21; // first two tile pixels are red and green.
    put16(s.vram.data(), 0x4000, 0);
    GbaVideo v;
    std::vector<uint32_t> out;
    v.Render(s, &out);
    assert(pixel(out, 0, 0) == RED);
    assert(pixel(out, 1, 0) == GREEN);
    assert(pixel(out, 2, 0) == BLUE);
    s.hofs[0] = 1;
    v.Render(s, &out);
    assert(pixel(out, 0, 0) == GREEN);
    s.hofs[0] = 0;
    put16(s.vram.data(), 0x4000, 0x400);
    v.Render(s, &out);
    assert(pixel(out, 6, 0) == GREEN);
    assert(pixel(out, 7, 0) == RED);
}

static void testBg8bppAndPriority() {
    VideoState s;
    s.dispcnt = 0x300;
    // BG0 and BG1 are both priority 1. BG0 wins ties.
    s.bgcnt[0] = (8u << 8) | 1;
    s.bgcnt[1] = (9u << 8) | 0x80u | 1;
    put16(s.palette.data(), 2, 0x001f);
    put16(s.palette.data(), 4, 0x03e0);
    s.vram[0] = 0x11; // BG0 uses red index 1.
    s.vram[1] = 1;    // BG1's first 8bpp tile pixel 1.
    put16(s.vram.data(), 0x4000, 0);
    put16(s.vram.data(), 0x4800, 0);
    GbaVideo v;
    std::vector<uint32_t> out;
    v.Render(s, &out);
    assert(pixel(out, 0, 0) == RED);
    s.bgcnt[0] = (8u << 8) | 2; // BG1 in front now.
    s.vram[0] = 2;             // BG1 index 2 green; BG0 index 2 green
    v.Render(s, &out);
    assert(pixel(out, 0, 0) == GREEN);
}

static void testObj4bppOrderAndFlip() {
    VideoState s;
    s.dispcnt = 0x1000 | 0x40; // OBJ, 1D mapping.
    put16(s.palette.data(), 0, 0);
    put16(s.palette.data(), 0x200 + 2, 0x001f);
    put16(s.palette.data(), 0x200 + 4, 0x03e0);
    s.vram[0x10000] = 0x21; // first two pixels: red/green
    for (int n = 0; n < 128; ++n) {
        put16(s.oam.data(), n * 8, 0x0200); // OBJ disabled by bit 9.
    }
    put16(s.oam.data(), 0, 10);            // OBJ 0 at (20,10)
    put16(s.oam.data(), 2, 20);
    put16(s.oam.data(), 4, 0);
    put16(s.oam.data(), 8, 10);            // OBJ 1 same location, palette index 2
    put16(s.oam.data(), 10, 20);
    put16(s.oam.data(), 12, 1);             // tile 1
    s.vram[0x10020] = 0x22;
    GbaVideo v;
    std::vector<uint32_t> out;
    v.Render(s, &out);
    assert(pixel(out, 20, 10) == RED); // lower OAM index wins
    assert(pixel(out, 21, 10) == GREEN);
    put16(s.oam.data(), 2, 20 | 0x1000u); // flip x
    v.Render(s, &out);
    assert(pixel(out, 26, 10) == GREEN);
    assert(pixel(out, 27, 10) == RED);
}

static void testObjAgainstBgPriority() {
    VideoState s;
    s.dispcnt = 0x1100 | 0x40;
    s.bgcnt[0] = (8u << 8) | 1;
    s.vram[0] = 1;
    put16(s.vram.data(), 0x4000, 0);
    put16(s.palette.data(), 2, 0x001f);
    put16(s.palette.data(), 0x200 + 2, 0x03e0);
    s.vram[0x10000] = 1;
    for (int n = 0; n < 128; ++n) put16(s.oam.data(), n * 8, 0x200);
    put16(s.oam.data(), 0, 0);
    put16(s.oam.data(), 2, 0);
    put16(s.oam.data(), 4, 0x0400); // OBJ priority 1, same as BG.
    GbaVideo v;
    std::vector<uint32_t> out;
    v.Render(s, &out);
    assert(pixel(out, 0, 0) == GREEN);
    put16(s.oam.data(), 4, 0x0800); // OBJ priority 2 => behind BG.
    v.Render(s, &out);
    assert(pixel(out, 0, 0) == RED);
}

static void testFrameClock() {
    wl4::FrameClock clock;
    constexpr int64_t start = 5000000000LL;
    assert(clock.Advance(start) == 1);
    assert(clock.Advance(start + 10000000LL) == 0);
    assert(clock.Advance(start + 20000000LL) == 1);
    assert(clock.Advance(start + 30000000LL) == 0);
    assert(clock.Advance(start + 40000000LL) == 1);
    assert(clock.Advance(start + 2000000000LL) == 1); // long pause drops backlog
    clock.Reset();
    assert(clock.Advance(start + 3000000000LL) == 1);
}

int main() {
    testBackdropAndForcedBlank();
    testBg4bppScrollFlipAndTransparency();
    testBg8bppAndPriority();
    testObj4bppOrderAndFlip();
    testObjAgainstBgPriority();
    testFrameClock();
    std::cout << "Native GBA video and frame clock OK\n";
}
