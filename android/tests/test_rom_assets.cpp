#include "../app/src/main/cpp/rom_assets.h"
#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>

int main() {
    std::vector<uint8_t> fake(128, 0);
    // Palette begins at byte 32. Color 1 is red, color 2 is green.
    fake[34] = 0x1f;
    fake[36] = 0xe0;
    fake[37] = 0x03;
    // First tile starts at byte 64. Pixel 0 -> index 1, pixel 1 -> index 2.
    fake[64] = 0x21;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    assert(wl4::Decode4bppTiles(fake.data(), fake.size(), 32, 64, 1, 1, &rgba, &w, &h));
    assert(w == 8 && h == 8 && rgba.size() == 8 * 8 * 4);
    assert(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
    assert(rgba[4] == 0 && rgba[5] == 255 && rgba[6] == 0 && rgba[7] == 255);
    assert(!wl4::Decode4bppTiles(fake.data(), fake.size(), 32, 110, 1, 1, &rgba, &w, &h));
    assert(!wl4::Decode4bppTiles(fake.data(), fake.size(), 32, 64, 1, 0, &rgba, &w, &h));
    std::cout << "ROM asset decoder OK" << std::endl;
}
