#include "gba_video.h"

#include <algorithm>

namespace wl4 {

uint16_t GbaVideo::Read16(const uint8_t* mem, size_t size, size_t offset) {
    if (offset > size || size - offset < 2) return 0;
    return static_cast<uint16_t>(mem[offset] | (static_cast<uint16_t>(mem[offset + 1]) << 8));
}

uint32_t GbaVideo::Color(const VideoState& state, uint32_t index) {
    // One palette is 256 entries: BG colors 0..255, OBJ colors 256..511.
    uint16_t c = Read16(state.palette.data(), state.palette.size(), index * 2);
    const uint32_t r = (c & 31u) * 255u / 31u;
    const uint32_t g = ((c >> 5) & 31u) * 255u / 31u;
    const uint32_t b = ((c >> 10) & 31u) * 255u / 31u;
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

void GbaVideo::DrawBackground(const VideoState& state, int bg,
                              std::vector<uint32_t>* rgba) {
    const uint16_t control = state.bgcnt[bg];
    const bool is8bpp = (control & 0x80u) != 0;
    const size_t charBase = static_cast<size_t>((control >> 2) & 3u) * 0x4000;
    const size_t screenBase = static_cast<size_t>((control >> 8) & 31u) * 0x800;
    const int size = (control >> 14) & 3;
    const int tilesWide = (size & 1) ? 64 : 32;
    const int tilesHigh = (size & 2) ? 64 : 32;
    const int mapWidthBlocks = tilesWide / 32;
    const int maskX = tilesWide * 8 - 1;
    const int maskY = tilesHigh * 8 - 1;

    for (int y = 0; y < VideoState::Height; ++y) {
        const int worldY = (y + state.vofs[bg]) & maskY;
        const int mapY = worldY >> 3;
        for (int x = 0; x < VideoState::Width; ++x) {
            const int worldX = (x + state.hofs[bg]) & maskX;
            const int mapX = worldX >> 3;
            const size_t block = static_cast<size_t>((mapY >> 5) * mapWidthBlocks +
                                                      (mapX >> 5));
            const size_t mapEntry = screenBase + block * 0x800 +
                static_cast<size_t>(((mapY & 31) * 32 + (mapX & 31)) * 2);
            if (mapEntry >= state.vram.size() ||
                state.vram.size() - mapEntry < 2) continue;
            const uint16_t tileInfo = Read16(state.vram.data(), state.vram.size(), mapEntry);
            const size_t tileIndex = tileInfo & 0x3ffu;
            int tileX = worldX & 7;
            int tileY = worldY & 7;
            if (tileInfo & 0x400u) tileX = 7 - tileX;
            if (tileInfo & 0x800u) tileY = 7 - tileY;
            const size_t tileSize = is8bpp ? 64 : 32;
            const size_t byteOffset = charBase + tileIndex * tileSize +
                static_cast<size_t>(tileY * (is8bpp ? 8 : 4) + (is8bpp ? tileX : tileX / 2));
            if (byteOffset >= state.vram.size()) continue;
            const uint8_t byte = state.vram[byteOffset];
            const uint8_t colorIndex = is8bpp ? byte :
                static_cast<uint8_t>(tileX & 1 ? (byte >> 4) : (byte & 0xfu));
            if (!colorIndex) continue;  // BG palette index 0 is transparent.
            const uint32_t bank = is8bpp ? 0u : ((tileInfo >> 12) & 15u) * 16u;
            (*rgba)[y * VideoState::Width + x] = Color(state, bank + colorIndex);
        }
    }
}

void GbaVideo::DrawObject(const VideoState& state, int index,
                          std::vector<uint32_t>* rgba) {
    const size_t entry = static_cast<size_t>(index) * 8;
    const uint16_t a0 = Read16(state.oam.data(), state.oam.size(), entry);
    const uint16_t a1 = Read16(state.oam.data(), state.oam.size(), entry + 2);
    const uint16_t a2 = Read16(state.oam.data(), state.oam.size(), entry + 4);

    // Regular sprites with bit 9 set are disabled; affine sprites still need a
    // matrix path and must not be accidentally drawn as regular sprites.
    if (a0 & 0x100u) return;
    if (a0 & 0x200u) return;
    const unsigned mode = (a0 >> 10) & 3u;
    if (mode == 2u || mode == 3u) return;  // OBJ window/prohibited
    const unsigned shape = (a0 >> 14) & 3u;
    const unsigned size = (a1 >> 14) & 3u;
    if (shape == 3) return;

    static constexpr int dims[3][4][2] = {
        {{8,8}, {16,16}, {32,32}, {64,64}},
        {{16,8}, {32,8}, {32,16}, {64,32}},
        {{8,16}, {8,32}, {16,32}, {32,64}}
    };
    const int width = dims[shape][size][0];
    const int height = dims[shape][size][1];

    // In bitmap video modes, OBJ tiles 0..511 overlap the bitmap framebuffer
    // and cannot be referenced as sprite art.
    if ((state.dispcnt & 7u) >= 3u && (a2 & 0x3ffu) < 512u) return;

    const int rawX = a1 & 511;
    const int rawY = a0 & 255;
    const int ox = rawX >= 256 ? rawX - 512 : rawX;
    const int oy = rawY >= 160 ? rawY - 256 : rawY;
    const bool is8bpp = (a0 & 0x2000u) != 0;
    const bool flipX = (a1 & 0x1000u) != 0;
    const bool flipY = (a1 & 0x2000u) != 0;
    const bool oneDimensional = (state.dispcnt & 0x40u) != 0;
    const size_t stride = oneDimensional ?
        static_cast<size_t>(width / 8) * (is8bpp ? 2u : 1u) : 32u;
    size_t firstTile = a2 & 0x3ffu;
    if (is8bpp) firstTile &= ~size_t{1};
    const unsigned palBank = (a2 >> 12) & 15u;

    const int fromX = std::max(0, ox);
    const int toX = std::min(VideoState::Width, ox + width);
    const int fromY = std::max(0, oy);
    const int toY = std::min(VideoState::Height, oy + height);
    if (fromX >= toX || fromY >= toY) return;

    for (int y = fromY; y < toY; ++y) {
        int py = y - oy;
        if (flipY) py = height - 1 - py;
        const size_t tileY = static_cast<size_t>(py >> 3);
        for (int x = fromX; x < toX; ++x) {
            int px = x - ox;
            if (flipX) px = width - 1 - px;
            const size_t tileX = static_cast<size_t>(px >> 3);
            const size_t tile = firstTile + tileY * stride +
                                tileX * (is8bpp ? 2u : 1u);
            const size_t byteOffset = 0x10000 + tile * 32u +
                static_cast<size_t>((py & 7) * (is8bpp ? 8 : 4) +
                                    (is8bpp ? (px & 7) : ((px & 7) >> 1)));
            if (byteOffset >= state.vram.size()) continue;
            const uint8_t byte = state.vram[byteOffset];
            const uint8_t pixel = is8bpp ? byte :
                static_cast<uint8_t>((px & 1) ? byte >> 4 : byte & 0x0f);
            if (pixel == 0) continue;
            const uint32_t color = 256u + (is8bpp ? 0u : palBank * 16u) + pixel;
            (*rgba)[y * VideoState::Width + x] = Color(state, color);
        }
    }
}


void GbaVideo::DrawBitmapBackground(const VideoState& state,
                                    std::vector<uint32_t>* rgba) {
    const unsigned mode = state.dispcnt & 7u;
    if (mode < 3 || mode > 5) return;
    const bool alternatePage = (state.dispcnt & 0x10u) != 0;
    const size_t base = (alternatePage && mode != 3) ? 0xa000u : 0;
    const int width = mode == 5 ? 160 : VideoState::Width;
    const int height = mode == 5 ? 128 : VideoState::Height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y * width + x);
            if (mode == 4) {
                const size_t addr = base + pixel;
                if (addr >= state.vram.size()) continue;
                // Bitmap mode 4 index 0 is opaque, unlike tiled BG pixels.
                (*rgba)[y * VideoState::Width + x] = Color(state, state.vram[addr]);
                continue;
            }
            const size_t addr = base + pixel * 2;
            if (addr >= state.vram.size() || state.vram.size() - addr < 2)
                continue;
            const uint16_t rgb = Read16(state.vram.data(), state.vram.size(), addr);
            const uint32_t red = (rgb & 31u) * 255u / 31u;
            const uint32_t green = ((rgb >> 5) & 31u) * 255u / 31u;
            const uint32_t blue = ((rgb >> 10) & 31u) * 255u / 31u;
            (*rgba)[y * VideoState::Width + x] =
                0xff000000u | (blue << 16) | (green << 8) | red;
        }
    }
}

void GbaVideo::Render(const VideoState& state, std::vector<uint32_t>* rgba) const {
    if (!rgba) return;
    rgba->assign(VideoState::Width * VideoState::Height, Color(state, 0));
    if (state.dispcnt & 0x80u) {
        std::fill(rgba->begin(), rgba->end(), 0xffffffffu);
        return;
    }
    const unsigned mode = state.dispcnt & 7u;
    if (mode == 1 || mode == 2 || mode >= 6) return;  // Affine modes TODO.

    // GBA ties: lower BG number is on top; OBJ with same priority is above BG.
    // For overlapping OBJ of equal priority, lower OAM index is in front.
    for (int priority = 3; priority >= 0; --priority) {
        for (int bg = 3; bg >= 0; --bg) {
            if (!(state.dispcnt & (0x100u << bg))) continue;
            if ((state.bgcnt[bg] & 3u) != static_cast<unsigned>(priority)) continue;
            if (mode == 0) DrawBackground(state, bg, rgba);
            else if (bg == 2) DrawBitmapBackground(state, rgba);
        }
        if (!(state.dispcnt & 0x1000u)) continue;
        for (int obj = 127; obj >= 0; --obj) {
            const size_t at = static_cast<size_t>(obj) * 8 + 4;
            const unsigned objPriority =
                (Read16(state.oam.data(), state.oam.size(), at) >> 10) & 3u;
            if (objPriority == static_cast<unsigned>(priority)) DrawObject(state, obj, rgba);
        }
    }
}

}  // namespace wl4
