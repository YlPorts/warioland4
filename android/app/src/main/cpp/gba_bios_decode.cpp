#include "gba_bios_decode.h"

#include <utility>

namespace wl4 {

bool DecodeGbaCompressed(Compression type, const uint8_t* source,
                         size_t sourceSize, size_t maxOutput,
                         std::vector<uint8_t>* out) {
    if (!out) return false;
    // A malformed stream must never leave a partially decoded output.
    if (!source || sourceSize < 4 || !maxOutput ||
        source[0] != static_cast<uint8_t>(type)) return false;
    const size_t decodedSize = static_cast<size_t>(source[1]) |
        (static_cast<size_t>(source[2]) << 8) |
        (static_cast<size_t>(source[3]) << 16);
    if (!decodedSize || decodedSize > maxOutput) return false;

    std::vector<uint8_t> decoded;
    decoded.reserve(decodedSize);
    size_t at = 4;
    if (type == Compression::Lz77) {
        while (decoded.size() < decodedSize) {
            if (at >= sourceSize) return false;
            const uint8_t flags = source[at++];
            for (int bit = 7; bit >= 0 && decoded.size() < decodedSize; --bit) {
                if ((flags & (1u << bit)) == 0) {
                    if (at >= sourceSize) return false;
                    decoded.push_back(source[at++]);
                    continue;
                }
                if (sourceSize - at < 2) return false;
                const uint8_t first = source[at++];
                const uint8_t second = source[at++];
                const size_t length = static_cast<size_t>(first >> 4) + 3;
                const size_t distance =
                    (static_cast<size_t>(first & 15) << 8) + second + 1;
                if (distance > decoded.size() ||
                    length > decodedSize - decoded.size()) return false;
                // Byte-wise copies intentionally allow overlapping matches.
                for (size_t i = 0; i < length; ++i)
                    decoded.push_back(decoded[decoded.size() - distance]);
            }
        }
    } else if (type == Compression::RunLength) {
        while (decoded.size() < decodedSize) {
            if (at >= sourceSize) return false;
            const uint8_t control = source[at++];
            if (control & 0x80) {
                const size_t length = static_cast<size_t>(control & 127) + 3;
                if (at >= sourceSize ||
                    length > decodedSize - decoded.size()) return false;
                decoded.insert(decoded.end(), length, source[at++]);
            } else {
                const size_t length = static_cast<size_t>(control & 127) + 1;
                if (length > decodedSize - decoded.size() ||
                    sourceSize - at < length) return false;
                decoded.insert(decoded.end(), source + at, source + at + length);
                at += length;
            }
        }
    } else {
        return false;
    }
    *out = std::move(decoded);
    return true;
}
}
