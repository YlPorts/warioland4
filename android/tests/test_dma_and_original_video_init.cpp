#include "../app/src/main/cpp/native_memory.h"
extern "C" {
#include "../app/src/main/cpp/wl4_host_game.h"
}
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

static int dmaFill(void* context, uint32_t destination,
                   uint32_t value, uint32_t bytes, uint32_t unitBytes) {
    if (!context) return 0;
    return static_cast<wl4::NativeMemory*>(context)->FillRegion(
        destination, value, bytes, unitBytes) ? 1 : 0;
}

static void testAtomicCopyFill() {
    wl4::NativeMemory memory;
    std::vector<uint8_t> rom(128);
    for (unsigned i = 0; i < rom.size(); ++i) rom[i] = static_cast<uint8_t>(i);
    assert(memory.LoadRom(std::move(rom)));
    assert(memory.CopyRegion(0x08000010,0x06000000,16,4));
    for (uint32_t i=0;i<16;++i) assert(memory.Read8(0x06000000+i)==i+16);
    assert(memory.CopyRegion(0x06000000,0x06000004,12,2));
    // overlapping copy reads all its source data before any writes
    for (uint32_t i=0;i<12;++i) assert(memory.Read8(0x06000004+i)==i+16);
    assert(memory.FillRegion(0x05000000,0x3a5a,8,2));
    for (uint32_t i=0;i<8;i+=2) {
        assert(memory.Read8(0x05000000+i)==0x5a);
        assert(memory.Read8(0x05000001+i)==0x3a);
    }
    assert(memory.FillRegion(0x07000000,0x12345678,12,4));
    assert(memory.Read32(0x07000000)==0x12345678);
    assert(memory.Read32(0x07000008)==0x12345678);

    // Failures are rejected before ANY destination byte changes.
    assert(!memory.FillRegion(0x050003fe,0xabab,4,2));
    assert(memory.Read16(0x050003fe)==0);
    assert(!memory.CopyRegion(0x0800007c,0x06000020,8,4));
    assert(memory.Read32(0x06000020)==0);
    assert(!memory.CopyRegion(0x08000010,0x06017ffc,8,4));
    assert(memory.Read32(0x06017ffc)==0);
    assert(!memory.CopyRegion(0x08000010,0x08000020,8,4));
    assert(!memory.CopyRegion(0x08000011,0x02000000,8,4));
    assert(!memory.FillRegion(0x02000001,0,4,2));
    assert(!memory.FillRegion(0x02000000,0,0,4));
    assert(!memory.FillRegion(0x04000000,0,4,4));
}

static void testOriginalVideoInitialization() {
    wl4::NativeMemory memory;
    // Seed every region with nonzero bytes.
    assert(memory.FillRegion(0x06000000,0xffffffffu,0x18000,4));
    assert(memory.FillRegion(0x07000000,0xffffffffu,0x400,4));
    assert(memory.FillRegion(0x05000000,0xffffffffu,0x400,4));
    wl4HostSetDmaFillHandler(&memory,dmaFill);
    InitializeVideoMemory(); // original src/init_helpers.c
    assert(memory.Read32(0x06000000)==0);
    assert(memory.Read32(0x06017ffc)==0);
    assert(memory.Read16(0x05000000)==0);
    assert(memory.Read16(0x050003fe)==0);
    // Original uses U8_MAX as a 32-bit pattern, NOT 0xffffffff.
    assert(memory.Read32(0x07000000)==0x000000ffu);
    assert(memory.Read32(0x070003fc)==0x000000ffu);
    wl4HostSetDmaFillHandler(nullptr,nullptr);
}

int main() {
    testAtomicCopyFill();
    testOriginalVideoInitialization();
    std::cout << "Native DMA copy/fill and original InitializeVideoMemory OK\n";
}
