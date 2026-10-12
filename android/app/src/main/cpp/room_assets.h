#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "native_memory.h"

namespace wl4 {

struct RoomBackground {
    uint16_t width = 0, height = 0;
    uint8_t screenSize = 0;
    std::vector<uint8_t> bytes;
};

// Validate both compressed byte planes before calling the original C decoder.
// Failure leaves the caller's output untouched.
bool DecodeRoomBackground(uint8_t type, const uint8_t* source, size_t sourceSize,
                          size_t maxOutput, RoomBackground* output);

struct RoomLayer {
    RoomBackground map;
    std::vector<std::array<uint16_t, 4>> tiles;
    std::vector<uint16_t> attributes;
};

// Original room headers are read field-by-field using their 44-byte GBA ABI,
// never cast to host structs (ARM64 pointers are 8 bytes).
class RoomAssets {
public:
    bool Load(const NativeMemory& memory, uint8_t stageId, uint8_t roomId,
              std::string* error = nullptr);
    void BuildVideo(int cameraX, int cameraY, VideoState* output) const;
    int PixelWidth() const { return layers_[1].map.width * 16; }
    int PixelHeight() const { return layers_[1].map.height * 16; }
    uint8_t RoomCount() const { return roomCount_; }
    uint8_t RoomId() const { return roomId_; }
    uint8_t StageId() const { return stageId_; }
    int InitialCameraX() const { return initialCameraX_; }
    int InitialCameraY() const { return initialCameraY_; }
    const RoomLayer& Layer(size_t index) const { return layers_.at(index); }

private:
    bool LoadInto(const NativeMemory& memory, uint8_t stageId, uint8_t roomId,
                  std::string* error);
    VideoState base_{};
    std::array<RoomLayer, 3> layers_{};
    std::array<uint8_t, 4> parameters_{};
    uint8_t scrolling_ = 0, roomCount_ = 0, roomId_ = 0, stageId_ = 0;
    int initialCameraX_ = 0, initialCameraY_ = 0;
};

} // namespace wl4
