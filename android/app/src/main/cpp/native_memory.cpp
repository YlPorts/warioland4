#include "native_memory.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace wl4 {
namespace {
constexpr uint32_t kKeyInput = 0x04000130;
// DMA uses physically contiguous memory. Refuse mirroring or crossing a bank
// boundary instead of emulating an arbitrary hardware bus access.
bool validSpan(uint32_t address, size_t bytes, size_t unit,
               bool writable, size_t romSize) {
    if (!bytes || bytes > 0x100000 || (unit != 2 && unit != 4) ||
        (bytes % unit) || (address % unit)) return false;
    const uint32_t bank = address >> 24;
    const size_t offset = address & 0xffffffu;
    size_t capacity = 0;
    switch (bank) {
        case 0x02: capacity = 0x40000; break;
        case 0x03: capacity = 0x8000; break;
        case 0x05: capacity = 0x400; break;
        case 0x06: capacity = 0x18000; break;
        case 0x07: capacity = 0x400; break;
        case 0x08: case 0x09: case 0x0a:
        case 0x0b: case 0x0c: case 0x0d:
            if (writable) return false;
            capacity = romSize;
            // Every ROM bank uses a separate 32MiB window.
            return (address & 0x1ffffffu) <= capacity &&
                   bytes <= capacity - (address & 0x1ffffffu);
        default: return false;
    }
    return offset <= capacity && bytes <= capacity - offset;
}
uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}
}

NativeMemory::NativeMemory() {
    sram_.fill(0xff);
    SetButtons(0);
}

const uint8_t* NativeMemory::RomRange(uint32_t address, size_t bytes) const {
    if (address < 0x08000000u || address >= 0x0a000000u || !bytes) return nullptr;
    const size_t offset = address - 0x08000000u;
    if (offset >= rom_.size() || bytes > rom_.size() - offset) return nullptr;
    return rom_.data() + offset;
}

bool NativeMemory::DecompressRomToMemory(size_t romOffset, uint32_t destination,
                                         Compression format, size_t maxOutput) {
    if (romOffset >= rom_.size() || !maxOutput) return false;
    const uint32_t bank = destination >> 24;
    const size_t offset = destination & 0xffffffu;
    size_t regionSize = 0;
    if (bank == 0x02) regionSize = EwramSize;
    else if (bank == 0x03) regionSize = IwramSize;
    else if (bank == 0x06) regionSize = 0x18000;
    else return false;

    if (offset >= regionSize) return false;
    const size_t available = regionSize - offset;
    // Upper bound is further constrained by destination and a global 1MiB cap.
    const size_t permitted = std::min({maxOutput, available, size_t{0x100000}});
    std::vector<uint8_t> decoded;
    if (!DecodeGbaCompressed(format, rom_.data() + romOffset,
                             rom_.size() - romOffset, permitted, &decoded)) return false;
    // Header validation above guarantees every write stays within one region.
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (!Write8(destination + static_cast<uint32_t>(i), decoded[i])) return false;
    }
    return true;
}

bool NativeMemory::FillRegion(uint32_t destination, uint32_t pattern,
                              size_t bytes, size_t unitBytes) {
    if (!validSpan(destination, bytes, unitBytes, true, rom_.size()))
        return false;
    for (size_t offset = 0; offset < bytes; offset += unitBytes) {
        // All writes are known valid before entering this loop.
        for (size_t b = 0; b < unitBytes; ++b) {
            if (!Write8(destination + static_cast<uint32_t>(offset + b),
                        static_cast<uint8_t>(pattern >> (8 * b)))) return false;
        }
    }
    return true;
}

bool NativeMemory::CopyRegion(uint32_t source, uint32_t destination,
                              size_t bytes, size_t unitBytes) {
    if (!validSpan(source, bytes, unitBytes, false, rom_.size()) ||
        !validSpan(destination, bytes, unitBytes, true, rom_.size()))
        return false;
    std::vector<uint8_t> snapshot(bytes);
    for (size_t i = 0; i < bytes; ++i)
        snapshot[i] = Read8(source + static_cast<uint32_t>(i));
    for (size_t i = 0; i < bytes; ++i) {
        if (!Write8(destination + static_cast<uint32_t>(i), snapshot[i]))
            return false;
    }
    return true;
}

bool NativeMemory::LoadRom(std::vector<uint8_t> rom) {
    if (rom.empty() || rom.size() > 0x2000000) return false;
    uint64_t fingerprint=14695981039346656037ull;
    for(uint8_t byte:rom){fingerprint^=byte;fingerprint*=1099511628211ull;}
    romFingerprint_=fingerprint;
    rom_ = std::move(rom);
    return true;
}

uint8_t NativeMemory::Read8(uint32_t address) const {
    // Top GBA bus byte maps onto an explicitly sized native allocation.
    // EWRAM/IWRAM are mirrored on hardware, including their high address bits.
    const uint32_t bank = address >> 24;
    switch (bank) {
        case 0x02: return ewram_[(address & 0xffffffu) % EwramSize];
        case 0x03: return iwram_[(address & 0xffffffu) % IwramSize];
        case 0x04:
            if ((address & 0xffffffu) < io_.size())
                return io_[address & 0xffffffu];
            break;
        case 0x05:
            if ((address & 0xffffffu) < palette_.size())
                return palette_[address & 0xffffffu];
            break;
        case 0x06: {
            uint32_t offset = address & 0x1ffffu;
            // VRAM has a 96KiB aperture; 0x18000..0x1ffff aliases the last 32KiB.
            if (offset >= 0x18000u) offset -= 0x8000u;
            return vram_[offset];
        }
        case 0x07: return oam_[address & 0x3ffu];
        case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: {
            // GBA ROM extends to 32MiB and has three read-only address windows.
            const uint32_t offset = address & 0x1ffffffu;
            if (offset < rom_.size()) return rom_[offset];
            break;
        }
        case 0x0e: case 0x0f: return sram_[address & 0xffffu];
        default: break;
    }
    return 0xff;
}

uint16_t NativeMemory::Read16(uint32_t address) const {
    address &= ~uint32_t{1};
    return static_cast<uint16_t>(Read8(address) |
          (static_cast<uint16_t>(Read8(address + 1)) << 8));
}
uint32_t NativeMemory::Read32(uint32_t address) const {
    address &= ~uint32_t{3};
    return static_cast<uint32_t>(Read16(address)) |
           (static_cast<uint32_t>(Read16(address + 2)) << 16);
}

bool NativeMemory::Write8(uint32_t address, uint8_t value) {
    const uint32_t bank = address >> 24;
    switch (bank) {
        case 0x02: ewram_[(address & 0xffffffu) % EwramSize] = value; return true;
        case 0x03: iwram_[(address & 0xffffffu) % IwramSize] = value; return true;
        case 0x04: {
            const uint32_t offset = address & 0xffffffu;
            if (offset >= io_.size() || offset == 0x130u || offset == 0x131u) return false;
            io_[offset] = value;
            return true;
        }
        case 0x05:
            if ((address & 0xffffffu) < palette_.size()) {
                palette_[address & 0xffffffu] = value;
                return true;
            }
            return false;
        case 0x06: {
            uint32_t offset = address & 0x1ffffu;
            if (offset >= 0x18000u) offset -= 0x8000u;
            vram_[offset] = value;
            return true;
        }
        case 0x07: oam_[address & 0x3ffu] = value; return true;
        case 0x0e: case 0x0f:
            sram_[address & 0xffffu] = value;
            saveDirty_ = true;
            return true;
        default: return false; // ROM and unmapped memory are read-only.
    }
}
bool NativeMemory::Write16(uint32_t address, uint16_t value) {
    address &= ~uint32_t{1};
    // Avoid partial writes at unmapped bank boundaries or to KEYINPUT.
    if ((address >> 24) != ((address + 1) >> 24) ||
        (address & ~uint32_t{1}) == kKeyInput ||
        ((address >> 24) != 0x02 && (address >> 24) != 0x03 &&
         (address >> 24) != 0x04 && (address >> 24) != 0x05 &&
         (address >> 24) != 0x06 && (address >> 24) != 0x07 &&
         (address >> 24) != 0x0e && (address >> 24) != 0x0f)) return false;
    return Write8(address, static_cast<uint8_t>(value)) &&
           Write8(address + 1, static_cast<uint8_t>(value >> 8));
}
bool NativeMemory::Write32(uint32_t address, uint32_t value) {
    address &= ~uint32_t{3};
    if ((address >> 24) != ((address + 3) >> 24) ||
        (address <= kKeyInput + 1 && address + 3 >= kKeyInput)) return false;
    // Check through Write16. Test explicitly for partial-region bounds.
    if ((address >> 24) == 0x04 && (address & 0xffffffu) + 4 > io_.size()) return false;
    if ((address >> 24) == 0x05 && (address & 0xffffffu) + 4 > palette_.size()) return false;
    return Write16(address, static_cast<uint16_t>(value)) &&
           Write16(address + 2, static_cast<uint16_t>(value >> 16));
}

void NativeMemory::SetButtons(uint16_t pressed) {
    buttonsHeld_ = pressed & 0x03ffu;
    uint16_t activeLow = static_cast<uint16_t>(~buttonsHeld_) | 0xfc00u;
    io_[kKeyInput - 0x04000000] = static_cast<uint8_t>(activeLow);
    io_[kKeyInput - 0x04000000 + 1] = static_cast<uint8_t>(activeLow >> 8);
}

void NativeMemory::InstallVideoState(const VideoState& state) {
    vram_ = state.vram;
    palette_ = state.palette;
    oam_ = state.oam;
    Write16(0x04000000, state.dispcnt);
    for (size_t i = 0; i < 4; ++i) {
        Write16(0x04000008 + static_cast<uint32_t>(i) * 2, state.bgcnt[i]);
        Write16(0x04000010 + static_cast<uint32_t>(i) * 4, state.hofs[i]);
        Write16(0x04000012 + static_cast<uint32_t>(i) * 4, state.vofs[i]);
    }
}

VideoState NativeMemory::CaptureVideo() const {
    VideoState snapshot;
    snapshot.vram = vram_;
    snapshot.palette = palette_;
    snapshot.oam = oam_;
    snapshot.dispcnt = readLe16(io_.data());
    for (size_t i = 0; i < 4; ++i) {
        snapshot.bgcnt[i] = readLe16(io_.data() + 8 + i * 2);
        snapshot.hofs[i] = readLe16(io_.data() + 0x10 + i * 4);
        snapshot.vofs[i] = readLe16(io_.data() + 0x12 + i * 4);
    }
    return snapshot;
}

bool NativeMemory::LoadSaveFile(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        sram_.fill(0xff);
        saveDirty_ = false;
        return true; // Fresh slot, nothing to restore.
    }
    if (in.tellg() != static_cast<std::streampos>(SaveSize)) return false;
    in.seekg(0);
    std::array<uint8_t, SaveSize> restored{};
    if (!in.read(reinterpret_cast<char*>(restored.data()), SaveSize)) return false;
    sram_ = restored;
    saveDirty_ = false;
    return true;
}

bool NativeMemory::FlushSaveFile(const std::string& path) {
    if (path.empty()) return false;
    if (!saveDirty_) return true;
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(sram_.data()), SaveSize);
        out.flush();
        if (!out.good()) {
            std::remove(temporary.c_str());
            return false;
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    saveDirty_ = false;
    return true;
}
}  // namespace wl4
