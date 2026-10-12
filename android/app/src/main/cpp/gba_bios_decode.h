#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wl4 {

// Native implementation of two GBA BIOS data formats, not an ARM7 interpreter.
// Input is bounded ROM data; decoded output is capped by maxOutput.
enum class Compression : uint8_t { Lz77 = 0x10, RunLength = 0x30 };
bool DecodeGbaCompressed(Compression type, const uint8_t* source,
                         size_t sourceSize, size_t maxOutput,
                         std::vector<uint8_t>* out);
}
