#include "rom_assets.h"

#include <limits>

namespace wl4 {
bool Decode4bppTiles(const uint8_t* rom, size_t romSize,
                     size_t paletteOffset, size_t tilesOffset,
                     size_t tileCount, size_t tilesPerRow,
                     std::vector<uint8_t>* rgba, int* outWidth, int* outHeight) {
    if (!rom || !rgba || !outWidth || !outHeight || !tileCount || !tilesPerRow ||
        tileCount > 4096 || tilesPerRow > 256 || paletteOffset > romSize ||
        romSize - paletteOffset < 32 || tilesOffset > romSize ||
        tileCount > (romSize - tilesOffset) / 32) {
        return false;
    }
    const size_t tileRows = (tileCount + tilesPerRow - 1) / tilesPerRow;
    if (tileRows > 4096 / 8) return false;
    const size_t width = tilesPerRow * 8, height = tileRows * 8;
    if (width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max()) return false;

    rgba->assign(width * height * 4, 0);
    for (size_t tile = 0; tile < tileCount; ++tile) {
        size_t px = (tile % tilesPerRow) * 8;
        size_t py = (tile / tilesPerRow) * 8;
        for (size_t y = 0; y < 8; ++y) {
            for (size_t x = 0; x < 8; ++x) {
                uint8_t packed = rom[tilesOffset + tile * 32 + y * 4 + x / 2];
                uint8_t index = (x & 1) ? packed >> 4 : packed & 0x0f;
                size_t pal = paletteOffset + static_cast<size_t>(index) * 2;
                uint16_t bgr555 = static_cast<uint16_t>(rom[pal]) |
                    static_cast<uint16_t>(rom[pal + 1] << 8);
                uint8_t r = static_cast<uint8_t>((bgr555 & 31) * 255 / 31);
                uint8_t g = static_cast<uint8_t>(((bgr555 >> 5) & 31) * 255 / 31);
                uint8_t b = static_cast<uint8_t>(((bgr555 >> 10) & 31) * 255 / 31);
                size_t pos = ((py + y) * width + px + x) * 4;
                (*rgba)[pos + 0] = r;
                (*rgba)[pos + 1] = g;
                (*rgba)[pos + 2] = b;
                (*rgba)[pos + 3] = 255; // atlas preview: display palette index zero
            }
        }
    }
    *outWidth = static_cast<int>(width);
    *outHeight = static_cast<int>(height);
    return true;
}
}
