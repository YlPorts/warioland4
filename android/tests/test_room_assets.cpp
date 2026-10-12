#include "../app/src/main/cpp/room_assets.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>

int main(int argc, char** argv) {
    // Four metatiles, with literal low bytes and repeated high bytes.
    const std::vector<uint8_t> shortRle = {2,2, 1,4,1,2,3,4,0, 1,0x84,0x12,0};
    wl4::RoomBackground output;
    assert(wl4::DecodeRoomBackground(1, shortRle.data(), shortRle.size(), 8, &output));
    assert(output.width == 2 && output.height == 2);
    assert((output.bytes == std::vector<uint8_t>{1,0x12,2,0x12,3,0x12,4,0x12}));
    assert(wl4::DecodeRoomBackground(2, shortRle.data(), shortRle.size(), 12, &output));
    assert((output.bytes == std::vector<uint8_t>{2,0,2,0,1,0x12,2,0x12,3,0x12,4,0x12}));
    const std::vector<uint8_t> longRle = {2,2, 2,0,4,1,2,3,4,0,0, 2,0x80,4,0x12,0,0};
    assert(wl4::DecodeRoomBackground(1, longRle.data(), longRle.size(), 8, &output));
    assert(output.bytes[7] == 0x12);
    const auto saved = output.bytes;
    for (size_t size = 0; size < longRle.size(); ++size) {
        assert(!wl4::DecodeRoomBackground(1, longRle.data(), size, 8, &output));
        assert(output.bytes == saved);
    }
    auto invalid = shortRle;
    invalid[3] = 5; // Literal run larger than the declared plane.
    assert(!wl4::DecodeRoomBackground(1, invalid.data(), invalid.size(), 8, &output));
    invalid = shortRle; invalid[10] = 0x83; // Incomplete high-byte plane.
    assert(!wl4::DecodeRoomBackground(1, invalid.data(), invalid.size(), 8, &output));
    assert(!wl4::DecodeRoomBackground(1, shortRle.data(), shortRle.size(), 7, &output));
    assert(!wl4::DecodeRoomBackground(3, shortRle.data(), shortRle.size(), 8, &output));
    // Full 32x32 tilemap, long runs with big-endian lengths.
    const std::vector<uint8_t> tilemap = {0, 2,0x84,0,0x40,0,0, 2,0x84,0,0,0,0};
    assert(wl4::DecodeRoomBackground(0, tilemap.data(), tilemap.size(), 0x800, &output));
    assert(output.width == 32 && output.height == 32 && output.bytes.size() == 0x800);
    assert(output.bytes[0] == 0x40 && output.bytes[0x7fe] == 0x40);

    wl4::NativeMemory memory;
    assert(memory.LoadRom(std::vector<uint8_t>(32, 0x77)));
    assert(memory.RomRange(0x08000000, 32) && !memory.RomRange(0x08000001, 32));
    assert(!memory.RomRange(0x09ffffff, 2) && !memory.RomRange(0x0a000000, 1));
    wl4::RoomAssets room;
    assert(!room.Load(memory, 0, 0));
    std::cout << "Bounded original room RLE and ROM address tests passed\n";
    if (argc < 2) return 0;
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    assert(memory.LoadRom(std::move(rom)));
    int loaded = 0, failed = 0;
    for (uint8_t stage = 0; stage < 29; ++stage) {
        const auto* record = memory.RomRange(0x08639068u + stage * 12u, 12);
        assert(record);
        if (record[0] >= 25) continue; // Separate minigame mode, not a room table.
        for (uint8_t index = 0; index < record[1]; ++index) {
            std::string error;
            if (!room.Load(memory, stage, index, &error)) {
                std::cerr << "Stage " << int(stage) << " room " << int(index) << ": " << error << '\n';
                ++failed; continue;
            }
            assert(room.PixelWidth() > 0 && room.PixelHeight() > 0);
            wl4::VideoState frame;
            room.BuildVideo(room.PixelWidth() - 240, room.PixelHeight() - 160, &frame);
            ++loaded;
        }
    }
    std::cout << loaded << " ROM rooms loaded; " << failed << " rejected\n";
    std::string error;
    assert(room.Load(memory, 0, 0, &error));
    assert(room.PixelWidth() == 656 && room.PixelHeight() == 368);
    assert(!room.Load(memory, 0, room.RoomCount(), &error));
    assert(room.RoomId() == 0 && room.PixelWidth() == 656); // Atomic failure.
    wl4::VideoState frame;
    room.BuildVideo(room.InitialCameraX(), room.InitialCameraY(), &frame);
    std::vector<uint32_t> pixels;
    wl4::GbaVideo().Render(frame, &pixels);
    assert(pixels.size() == 240 * 160);
    if (argc > 2) {
        std::ofstream image(argv[2], std::ios::binary);
        image << "P6\n240 160\n255\n";
        for (uint32_t color : pixels) {
            const char rgb[] = {char(color), char(color >> 8), char(color >> 16)};
            image.write(rgb, 3);
        }
    }
    return failed ? 1 : 0;
}
