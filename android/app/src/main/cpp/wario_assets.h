#pragma once
#include "native_memory.h"
#include <array>
#include <vector>
#include <string>

namespace wl4 {
struct WarioFrame {
    std::vector<uint8_t> upper, lower;
    std::vector<std::array<uint16_t,3>> objects;
    uint8_t duration = 1;
};

class WarioAssets {
public:
    bool Load(const NativeMemory& memory, std::string* error = nullptr);
    const std::vector<WarioFrame>& Animation(uint8_t pose, bool right, bool descending) const;
    std::array<int,4> Hitbox(uint8_t pose) const;
    void Draw(uint8_t pose, bool right, bool descending, size_t frame,
              int x, int y, VideoState* video) const;
private:
    bool ReadAnimation(const NativeMemory& memory, uint32_t address,
                       std::vector<WarioFrame>* output);
    std::array<std::array<std::vector<WarioFrame>,2>,80> animations_{};
    std::array<std::array<int,4>,78> hitboxes_{};
    std::array<uint8_t,96> palette_{}, bashPalette_{};
};
} // namespace wl4
