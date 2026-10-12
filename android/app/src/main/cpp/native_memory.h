#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gba_video.h"

namespace wl4 {

// Android-host address space for decompiled game subsystems.
// NOT a CPU emulator and NOT an unsafe mmap of GBA addresses: callers
// explicitly translate bus reads/writes to portable owned buffers.
class NativeMemory {
public:
    static constexpr size_t EwramSize = 0x40000;
    static constexpr size_t IwramSize = 0x8000;
    static constexpr size_t SaveSize = 0x10000;

    NativeMemory();
    bool LoadRom(std::vector<uint8_t> rom);
    bool HasRom() const { return !rom_.empty(); }
    size_t RomSize() const { return rom_.size(); }

    uint8_t Read8(uint32_t address) const;
    uint16_t Read16(uint32_t address) const;
    uint32_t Read32(uint32_t address) const;
    bool Write8(uint32_t address, uint8_t value);
    bool Write16(uint32_t address, uint16_t value);
    bool Write32(uint32_t address, uint32_t value);
    void SetButtons(uint16_t pressed);
    uint16_t ButtonsHeld() const { return buttonsHeld_; }
    VideoState CaptureVideo() const;
    // Used by the temporary native diagnostics; later the game will write
    // video registers/VRAM itself through the platform-facing interface.
    void InstallVideoState(const VideoState& state);

    // Buffered battery SRAM, to be wired into game saves in a later stage.
    // No gameplay save writes occur until real SRAM game calls are ported.
    bool LoadSaveFile(const std::string& path);
    bool FlushSaveFile(const std::string& path);
    bool SaveDirty() const { return saveDirty_; }

private:
    std::array<uint8_t, EwramSize> ewram_{};
    std::array<uint8_t, IwramSize> iwram_{};
    std::array<uint8_t, 0x400> io_{};
    std::array<uint8_t, 0x400> palette_{};
    std::array<uint8_t, 0x18000> vram_{};
    std::array<uint8_t, 0x400> oam_{};
    std::array<uint8_t, SaveSize> sram_{};
    std::vector<uint8_t> rom_;
    uint16_t buttonsHeld_ = 0;
    bool saveDirty_ = false;
};

}  // namespace wl4
