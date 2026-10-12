#include "../app/src/main/cpp/gba_video.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using wl4::GbaVideo;
using wl4::VideoState;
static uint32_t at(const std::vector<uint32_t>& p, int x, int y) {
    return p[y * VideoState::Width + x];
}
static void put16(uint8_t* p, size_t a, uint16_t v) {
    p[a] = static_cast<uint8_t>(v);
    p[a + 1] = static_cast<uint8_t>(v >> 8);
}
static constexpr uint32_t RED = 0xff0000ffu;
static constexpr uint32_t GREEN = 0xff00ff00u;
static constexpr uint32_t BLUE = 0xffff0000u;
static constexpr uint32_t BLACK = 0xff000000u;

static void testMode3() {
    VideoState s;
    s.dispcnt = 3 | (1 << 10);
    put16(s.palette.data(), 0, 0x7c00);
    put16(s.vram.data(), (0 * 240 + 0) * 2, 0x001f);
    put16(s.vram.data(), (159 * 240 + 239) * 2, 0x03e0);
    std::vector<uint32_t> p;
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == RED);
    assert(at(p, 239, 159) == GREEN);
    assert(at(p, 1, 0) == BLACK);
}
static void testMode4PagesAndIndexZero() {
    VideoState s;
    s.dispcnt = 4 | (1 << 10);
    put16(s.palette.data(), 0, 0x7c00);  // index zero is opaque BLUE
    put16(s.palette.data(), 2, 0x001f);
    s.vram[0] = 1;
    s.vram[0xa000] = 0;
    s.vram[0xa001] = 1;
    std::vector<uint32_t> p;
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == RED);
    s.dispcnt |= 0x10;
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == BLUE);
    assert(at(p, 1, 0) == RED);
}
static void testMode5ViewportAndAlternatePage() {
    VideoState s;
    s.dispcnt = 5 | (1 << 10) | 0x10;
    put16(s.palette.data(), 0, 0x7c00);
    put16(s.vram.data(), 0xa000, 0x03e0);
    put16(s.vram.data(), 0xa000 + (127 * 160 + 159) * 2, 0x001f);
    std::vector<uint32_t> p;
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == GREEN);
    assert(at(p, 159, 127) == RED);
    assert(at(p, 160, 127) == BLUE);
    assert(at(p, 0, 128) == BLUE);
    s.dispcnt &= ~0x10;
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == BLACK);
}
static void testBitmapObjectTileProtection() {
    VideoState s;
    s.dispcnt = 3 | (1 << 10) | (1 << 12);
    // Tile #0 is illegal in bitmap sprite modes; tile #512 is legal.
    put16(s.palette.data(), 0x200 + 2, 0x03e0);
    s.vram[0x14000] = 1;
    for (int n = 0; n < 128; ++n)
        put16(s.oam.data(), n * 8, 0x0200); // disable unused objects
    put16(s.oam.data(), 0, 0);
    put16(s.oam.data(), 2, 0);
    put16(s.oam.data(), 4, 0);
    std::vector<uint32_t> p;
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == BLACK);
    put16(s.oam.data(), 4, 512);
    GbaVideo{}.Render(s, &p);
    assert(at(p, 0, 0) == GREEN);
}

int main() {
    testMode3();
    testMode4PagesAndIndexZero();
    testMode5ViewportAndAlternatePage();
    testBitmapObjectTileProtection();
    std::cout << "Native bitmap BG mode 3/4/5 and OBJ bounds OK\n";
}
