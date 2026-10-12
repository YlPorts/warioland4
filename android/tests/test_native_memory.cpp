#include "../app/src/main/cpp/native_memory.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>

using wl4::NativeMemory;

void testMemoryMapping() {
    NativeMemory m;
    assert(!m.HasRom());
    std::vector<uint8_t> rom(0x40000);
    rom[0x1234] = 0x42;
    assert(m.LoadRom(std::move(rom)));
    assert(m.HasRom() && m.RomSize() == 0x40000);
    assert(m.Read8(0x08001234) == 0x42);
    assert(m.Read8(0x0a001234) == 0x42);
    assert(m.Read8(0x0c001234) == 0x42);
    assert(!m.Write8(0x08001234, 1));
    assert(m.Write32(0x02000000, 0x12345678));
    assert(m.Read32(0x02000000) == 0x12345678);
    assert(m.Read32(0x02040000) == 0x12345678);
    assert(m.Write16(0x03000000, 0xabcd));
    assert(m.Read16(0x03008000) == 0xabcd);
    assert(m.Write16(0x05000000, 0x7fff));
    assert(m.Read16(0x05000000) == 0x7fff);
    assert(m.Write8(0x06010010, 0x29));
    assert(m.Read8(0x06018010) == 0x29);
    assert(m.Write16(0x07000008, 0x100));
    assert(m.Read16(0x07000408) == 0x100);
    assert(m.Read8(0x01000000) == 0xff);
    assert(!m.Write32(0x08000000, 0xdeadbeef));
}

void testKeyInput() {
    NativeMemory m;
    assert(m.Read16(0x04000130) == 0xffff);
    m.SetButtons(1 | 0x10 | 0x200);
    assert(m.Read16(0x04000130) == (0xffffu & ~(1u | 0x10u | 0x200u)));
    assert(!m.Write16(0x04000130, 0));
    assert(!m.Write32(0x04000130, 0));
    m.SetButtons(0);
    assert(m.Read16(0x04000130) == 0xffff);
}

void testVideoSnapshot() {
    NativeMemory m;
    assert(m.Write16(0x04000000, 0x1140));
    assert(m.Write16(0x04000008, 0x0802));
    assert(m.Write16(0x04000010, 17));
    assert(m.Write16(0x04000012, 23));
    assert(m.Write16(0x05000002, 0x001f));
    assert(m.Write8(0x06000000, 0x21));
    assert(m.Write16(0x07000000, 12));
    auto state = m.CaptureVideo();
    assert(state.dispcnt == 0x1140);
    assert(state.bgcnt[0] == 0x0802);
    assert(state.hofs[0] == 17);
    assert(state.vofs[0] == 23);
    assert(state.vram[0] == 0x21);
    assert(state.palette[2] == 0x1f);
    assert(state.oam[0] == 12);
    NativeMemory copy;
    copy.InstallVideoState(state);
    auto roundtrip = copy.CaptureVideo();
    assert(roundtrip.dispcnt == state.dispcnt);
    assert(roundtrip.bgcnt[0] == state.bgcnt[0]);
    assert(roundtrip.hofs[0] == state.hofs[0]);
    assert(roundtrip.vofs[0] == state.vofs[0]);
    assert(roundtrip.oam[0] == state.oam[0]);
    assert(roundtrip.vram[0] == state.vram[0]);
}

void testSram() {
    const std::string path = "/tmp/wl4-test-native-sram-20261012.sav";
    std::remove(path.c_str());
    NativeMemory m;
    assert(m.LoadSaveFile(path));
    assert(m.Read8(0x0e000012) == 0xff);
    assert(m.Write8(0x0e000012, 0x5a));
    assert(m.SaveDirty());
    assert(m.FlushSaveFile(path));
    assert(!m.SaveDirty());
    NativeMemory restored;
    assert(restored.LoadSaveFile(path));
    assert(restored.Read8(0x0e000012) == 0x5a);
    assert(restored.Read8(0x0f000012) == 0x5a);
    std::remove(path.c_str());
}

int main() {
    testMemoryMapping();
    testKeyInput();
    testVideoSnapshot();
    testSram();
    std::cout << "Native memory/video/keys/SRAM host tests OK\n";
}
