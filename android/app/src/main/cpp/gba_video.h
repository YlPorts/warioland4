#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wl4 {

// Host-native presentation of GBA video memory for the future C game engine.
// No GBA CPU interpreter is involved. Pixel output is RGBA8888 in host little-endian.
struct VideoState {
    static constexpr int Width = 240;
    static constexpr int Height = 160;
    std::array<uint8_t, 0x18000> vram{};
    std::array<uint8_t, 0x400> palette{};
    std::array<uint8_t, 0x400> oam{};

    uint16_t dispcnt = 0;
    std::array<uint16_t, 4> bgcnt{};
    std::array<uint16_t, 4> hofs{};
    std::array<uint16_t, 4> vofs{};
};

// Initial backend: tiled mode 0, BG0..3 (4/8bpp), 1D/2D normal OBJ,
// tile flips, scroll, palette banks and equal-priority ordering.
// Affine BG/OBJ, windows, mosaic and blending are not implemented.
class GbaVideo {
public:
    void Render(const VideoState& state, std::vector<uint32_t>* rgba) const;

private:
    static uint16_t Read16(const uint8_t* mem, size_t size, size_t offset);
    static uint32_t Color(const VideoState& state, uint32_t paletteIndex);
    static void DrawBackground(const VideoState& state, int bg,
                               std::vector<uint32_t>* rgba);
    static void DrawBitmapBackground(const VideoState& state,
                                     std::vector<uint32_t>* rgba);
    static void DrawObject(const VideoState& state, int index,
                           std::vector<uint32_t>* rgba);
};

}  // namespace wl4
