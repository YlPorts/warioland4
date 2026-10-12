#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wl4 {
// Decodes GBA 4bpp tiles and BGR555 palettes to RGBA8888.
// This is native resource decoding, NOT GBA CPU emulation.
bool Decode4bppTiles(
    const uint8_t* rom,
    size_t romSize,
    size_t paletteOffset,
    size_t tilesOffset,
    size_t tileCount,
    size_t tilesPerRow,
    std::vector<uint8_t>* outRgba,
    int* outWidth,
    int* outHeight);
}
