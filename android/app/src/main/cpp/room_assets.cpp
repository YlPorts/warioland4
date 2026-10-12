#include "room_assets.h"
#include <algorithm>
#include <cstring>
#include <utility>
extern "C" {
#include "wl4_host_game.h"
}

namespace wl4 {
namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t le32(const uint8_t* p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }
void put16(uint8_t* p, uint16_t value) { p[0] = uint8_t(value); p[1] = uint8_t(value >> 8); }
bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}
bool decodeAt(const NativeMemory& memory, uint32_t address, uint8_t type,
              RoomBackground* output) {
    const auto* source = memory.RomRange(address, 1);
    if (!source) return false;
    return DecodeRoomBackground(type, source,
        memory.RomSize() - (address - 0x08000000u), 0x20000, output);
}
bool copyAt(const NativeMemory& memory, uint32_t address, size_t count,
            uint8_t* output, size_t capacity) {
    if (count > capacity) return false;
    if (!count) return true;
    const auto* source = memory.RomRange(address, count);
    if (!source) return false;
    std::memcpy(output, source, count);
    return true;
}
}

bool DecodeRoomBackground(uint8_t type, const uint8_t* source, size_t sourceSize,
                          size_t maxOutput, RoomBackground* output) {
    if (!source || !output || !sourceSize || type > 2) return false;
    RoomBackground decoded;
    size_t cursor = 0, prefix = 0, bytes = 0;
    if (type == 0) {
        decoded.screenSize = source[cursor++];
        if (decoded.screenSize > 3) return false;
        decoded.width = (decoded.screenSize & 1) ? 64 : 32;
        decoded.height = (decoded.screenSize & 2) ? 64 : 32;
        bytes = size_t(decoded.width) * decoded.height * 2;
    } else {
        if (sourceSize < 2) return false;
        decoded.width = source[cursor++];
        decoded.height = source[cursor++];
        if (!decoded.width || !decoded.height) return false;
        bytes = size_t(decoded.width) * decoded.height * 2;
        if (type == 2) prefix = 4;
    }
    if (maxOutput > 0x100000 || bytes + prefix > maxOutput) return false;
    // The format encodes low/high bytes in separate RLE passes with either
    // 8-bit or big-endian 16-bit run lengths. Each plane must end exactly at
    // width*height, preventing the original unbounded decoder from overrunning.
    for (int plane = 0; plane < 2; ++plane) {
        if (cursor >= sourceSize) return false;
        const bool shortRuns = source[cursor++] == 1;
        size_t written = 0;
        for (;;) {
            const size_t countBytes = shortRuns ? 1 : 2;
            if (sourceSize - cursor < countBytes) return false;
            uint32_t run = source[cursor++];
            if (!shortRuns) run = (run << 8) | source[cursor++];
            if (!run) break;
            const uint32_t flag = shortRuns ? 0x80u : 0x8000u;
            const bool repeat = (run & flag) != 0;
            const size_t count = run & (flag - 1);
            if (count > bytes / 2 - written) return false;
            const size_t input = repeat ? 1 : count;
            if (input > sourceSize - cursor) return false;
            cursor += input;
            written += count;
        }
        if (written != bytes / 2) return false;
    }
    decoded.bytes.resize(bytes + prefix);
    DecompressRoomBackground(type, source, decoded.bytes.data());
    *output = std::move(decoded);
    return true;
}

bool RoomAssets::Load(const NativeMemory& memory, uint8_t stageId, uint8_t roomId,
                     std::string* error) {
    RoomAssets next;
    if (!next.LoadInto(memory, stageId, roomId, error)) return false;
    *this = std::move(next);
    if (error) error->clear();
    return true;
}

bool RoomAssets::LoadInto(const NativeMemory& memory, uint8_t stageId, uint8_t roomId,
                         std::string* error) {
    const auto* gameCode = memory.RomRange(0x080000acu, 4);
    if (!gameCode || (std::memcmp(gameCode, "AWAE", 4) &&
                      std::memcmp(gameCode, "AWAJ", 4)))
        return fail(error, "Unrecognized Wario Land 4 ROM layout");
    if (stageId >= 29) return fail(error, "Stage outside ROM stage table");
    const auto* stage = memory.RomRange(0x08639068u + stageId * 12u, 12);
    if (!stage || stage[0] >= 25 || !stage[1] || roomId >= stage[1])
        return fail(error, "Invalid room or unsupported minigame");
    const auto* table = memory.RomRange(0x0878f280u + stage[0] * 4u, 4);
    if (!table) return fail(error, "Missing room pointer table");
    const uint64_t headerAddress = uint64_t(le32(table)) + roomId * 44u;
    if (headerAddress > UINT32_MAX) return fail(error, "Room pointer overflow");
    const auto* header = memory.RomRange(uint32_t(headerAddress), 44);
    if (!header) return fail(error, "Room header outside ROM");
    const auto* tileset = memory.RomRange(0x083f2298u + header[0] * 36u, 36);
    if (!tileset) return fail(error, "Tileset outside ROM");
    for (size_t i = 0; i < 4; ++i) parameters_[i] = header[i + 1];
    scrolling_ = header[25];
    const size_t graphicsSize = le16(tileset + 4);
    const size_t animatedSize = le16(tileset + 16);
    if (graphicsSize > 0xb7c0 || animatedSize > 0xb7c0 ||
        (graphicsSize & 31) || (animatedSize & 31))
        return fail(error, "Invalid tileset graphics sizes");
    // Original LoadRoom destinations: char blocks, blank tile and palettes.
    if (!copyAt(memory, le32(tileset), graphicsSize,
                base_.vram.data() + 0x4820, base_.vram.size() - 0x4820) ||
        !copyAt(memory, le32(tileset + 8), 0x200, base_.palette.data(), 0x200) ||
        !copyAt(memory, le32(tileset + 12), animatedSize,
                base_.vram.data() + 0xffe0 - animatedSize, animatedSize) ||
        !copyAt(memory, 0x083f2020u, 32, base_.vram.data() + 0x4800, 32) ||
        !copyAt(memory, 0x083f2020u, 32, base_.vram.data() + 0xffe0, 32))
        return fail(error, "Graphics pointer outside ROM");
    base_.palette[0] = base_.palette[1] = 0;
    for (int i = 0; i < 128; ++i) put16(base_.oam.data() + i * 8, 0x0200);

    const uint32_t metatiles = le32(tileset + 20);
    const uint32_t collisionTable = le32(tileset + 24);
    const uint32_t attributes = le32(tileset + 28);
    for (size_t layer = 0; layer < 3; ++layer) {
        if (!(parameters_[layer] & 0x10)) continue;
        RoomLayer& data = layers_[layer];
        if (!decodeAt(memory, le32(header + 8 + layer * 4), 1, &data.map))
            return fail(error, "Invalid room metatile RLE");
        const size_t count = size_t(data.map.width) * data.map.height;
        data.tiles.resize(count);
        data.attributes.resize(count);
        data.collision.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const uint16_t id = le16(data.map.bytes.data() + i * 2);
            if (id > 0x3ff) return fail(error, "Metatile index outside tileset");
            const uint64_t tileAddress = uint64_t(metatiles) + id * 8u;
            const uint64_t attrAddress = uint64_t(attributes) + id * 2u;
            if (tileAddress > UINT32_MAX || attrAddress > UINT32_MAX)
                return fail(error, "Metatile pointer overflow");
            const auto* tile = memory.RomRange(uint32_t(tileAddress), 8);
            const auto* attribute = memory.RomRange(uint32_t(attrAddress), 2);
            const uint64_t collisionAddress = uint64_t(collisionTable) + id;
            const auto* collision = collisionAddress <= UINT32_MAX ?
                memory.RomRange(uint32_t(collisionAddress), 1) : nullptr;
            if (!tile || !attribute || !collision) return fail(error, "Metatile table outside ROM");
            for (size_t k = 0; k < 4; ++k) data.tiles[i][k] = le16(tile + k * 2);
            data.attributes[i] = le16(attribute);
            data.collision[i] = *collision;
        }
    }
    if (layers_[1].tiles.empty()) return fail(error, "Room missing primary background");
    RoomBackground background;
    if (!decodeAt(memory, le32(header + 20), 0, &background) ||
        background.bytes.size() > 0x1000)
        return fail(error, "Invalid room BG3 tilemap");
    std::copy(background.bytes.begin(), background.bytes.end(), base_.vram.begin() + 0x3000);
    base_.bgcnt = {{0x4004, 0x4205, 0x4406, 0x0603}};
    // Priorities are the original ConfigureRoomDisplay ordering.
    if (header[26] >= 5) {
        const unsigned mode = (header[26] - 5) % 4;
        if (mode <= 1) { base_.bgcnt[0] |= 1; base_.bgcnt[1] &= ~3u; }
        else if (mode == 2) {
            base_.bgcnt[0] |= 2; base_.bgcnt[1] &= ~3u;
            base_.bgcnt[2] = uint16_t((base_.bgcnt[2] & ~3u) | 1u);
        }
    }
    base_.bgcnt[3] |= uint16_t(background.screenSize << 14);
    base_.bgcnt[3] |= parameters_[3] == 0x20 ? 8 : 4;
    base_.dispcnt = 0x40 | 0x0800;
    for (size_t layer = 0; layer < 3; ++layer) {
        if (parameters_[layer] & 0x10) base_.dispcnt |= uint16_t(0x100u << layer);
        else if (layer == 0 && (parameters_[0] & 0x20)) {
            RoomBackground map;
            if (!decodeAt(memory, le32(header + 8), 0, &map) || map.bytes.size() > 0x1000)
                return fail(error, "Invalid room BG0 tilemap");
            std::copy(map.bytes.begin(), map.bytes.end(), base_.vram.begin());
            base_.bgcnt[0] = uint16_t((map.screenSize << 14) | 8 | (base_.bgcnt[0] & 3));
            base_.dispcnt |= 0x100;
        }
    }
    roomCount_ = stage[1]; roomId_ = roomId; stageId_ = stageId;
    roomTableIndex_ = stage[0];
    const auto* entryTable = memory.RomRange(0x0878f21cu + stage[0] * 4u, 4);
    const auto* entry = entryTable ? memory.RomRange(le32(entryTable), 12) : nullptr;
    if (!entry) return fail(error, "Room entry table outside ROM");
    if (entry[1] == roomId) {
        // Center the preview near the original entrance; this is not the
        // original camera logic or a simulated player position.
        const int x = entry[2] * 16 + int(int8_t(entry[7])) + 8;
        const int y = (entry[5] + 1) * 16 + int(int8_t(entry[8])) - 1;
        initialCameraX_ = std::clamp(x - 120, 0, std::max(0, PixelWidth() - 240));
        initialCameraY_ = std::clamp(y - 96, 0, std::max(0, PixelHeight() - 160));
    }
    return true;
}

uint16_t RoomAssets::AttributeAt(size_t layer, int column, int row) const {
    const auto& data = layers_.at(layer);
    if (column < 0 || row < 0 || column >= data.map.width || row >= data.map.height) return 0;
    return data.attributes[size_t(row) * data.map.width + column];
}

uint8_t RoomAssets::CollisionAt(size_t layer, int column, int row) const {
    const auto& data = layers_.at(layer);
    if (column < 0 || row < 0 || column >= data.map.width || row >= data.map.height) return 0;
    return data.collision[size_t(row) * data.map.width + column];
}

bool RoomAssets::ClearTile(int column, int row) {
    auto& data = layers_[1];
    if (column < 0 || row < 0 || column >= data.map.width || row >= data.map.height) return false;
    const size_t index = size_t(row) * data.map.width + column;
    data.tiles[index] = {{0x40,0x40,0x40,0x40}};
    data.collision[index] = 0;
    data.attributes[index] = 0;
    data.map.bytes[index * 2] = data.map.bytes[index * 2 + 1] = 0;
    return true;
}

void RoomAssets::BuildVideo(int cameraX, int cameraY, VideoState* output) const {
    if (!output) return;
    *output = base_;
    const int x = std::clamp(cameraX, 0, std::max(0, PixelWidth() - VideoState::Width));
    const int y = std::clamp(cameraY, 0, std::max(0, PixelHeight() - VideoState::Height));
    for (size_t layer = 0; layer < 3; ++layer) {
        const auto& data = layers_[layer];
        if (data.tiles.empty()) continue;
        auto* map = output->vram.data() + layer * 0x1000;
        for (size_t offset = 0; offset < 0x1000; offset += 2) put16(map + offset, 0x0040);
        for (int row = y / 16; row <= y / 16 + 11; ++row) {
            for (int col = x / 16; col <= x / 16 + 16; ++col) {
                if (col >= data.map.width || row >= data.map.height) continue;
                const auto& tile = data.tiles[size_t(row) * data.map.width + col];
                const size_t block = (col & 16) ? 0x800 : 0;
                const size_t offset = block + size_t(row & 15) * 128 + size_t(col & 15) * 4;
                put16(map + offset, tile[0]); put16(map + offset + 2, tile[1]);
                put16(map + offset + 64, tile[2]); put16(map + offset + 66, tile[3]);
            }
        }
        output->hofs[layer] = uint16_t(x & 511);
        output->vofs[layer] = uint16_t(y & 255);
    }
    const auto scroll = [](int value, int mode) { return mode == 1 ? value : mode == 2 ? value / 2 : 0; };
    static constexpr int modes[7][2] = {{0,0},{2,0},{0,2},{2,2},{1,2},{2,1},{1,1}};
    if (scrolling_ < 7) {
        output->hofs[3] = uint16_t(scroll(x, modes[scrolling_][0]));
        output->vofs[3] = uint16_t(scroll(y, modes[scrolling_][1]));
    }
}

} // namespace wl4
