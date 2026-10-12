#include "../app/src/main/cpp/gba_bios_decode.h"
#include "../app/src/main/cpp/native_memory.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace wl4;

static void testLz77() {
    // ABC followed by length=9, distance=3: overlapping back-reference.
    const uint8_t packed[] = {0x10, 12, 0, 0, 0x10, 'A', 'B', 'C', 0x60, 0x02};
    std::vector<uint8_t> out{0xee};
    assert(DecodeGbaCompressed(Compression::Lz77, packed, sizeof packed, 12, &out));
    assert(out.size() == 12);
    for (size_t i=0; i<out.size(); ++i) assert(out[i] == "ABC"[i%3]);
    const auto saved = out;
    assert(!DecodeGbaCompressed(Compression::Lz77, packed, sizeof packed-1, 12, &out));
    assert(out == saved); // Invalid data must not mutate existing output.
    assert(!DecodeGbaCompressed(Compression::Lz77, packed, sizeof packed, 11, &out));
    const uint8_t badBackref[] = {0x10,3,0,0, 0x80,0x00,0x00};
    assert(!DecodeGbaCompressed(Compression::Lz77,badBackref,sizeof badBackref,3,&out));
    const uint8_t badLength[] = {0x10,1,0,0,0x80,0,0};
    assert(!DecodeGbaCompressed(Compression::Lz77,badLength,sizeof badLength,3,&out));
}
static void testRle() {
    const uint8_t packed[] = {0x30, 8,0,0, 0x81,'Z', 0x03,'A','B','C','D'};
    std::vector<uint8_t> out;
    assert(DecodeGbaCompressed(Compression::RunLength, packed,sizeof packed,32,&out));
    assert(out == (std::vector<uint8_t>{'Z','Z','Z','Z','A','B','C','D'}));
    assert(!DecodeGbaCompressed(Compression::RunLength, packed,sizeof packed-1,32,&out));
    const uint8_t bad[] = {0x30, 2,0,0, 0x80,'X'};
    assert(!DecodeGbaCompressed(Compression::RunLength,bad,sizeof bad,16,&out));
}
static void testRomToVideo() {
    std::vector<uint8_t> rom(64,0);
    const uint8_t packed[] = {0x10,12,0,0,0x10,'A','B','C',0x60,0x02};
    for (size_t i=0;i<sizeof packed;++i) rom[16+i]=packed[i];
    NativeMemory memory;
    assert(memory.LoadRom(std::move(rom)));
    assert(memory.DecompressRomToMemory(16,0x06000040,Compression::Lz77,128));
    assert(memory.Read8(0x06000040) == 'A');
    assert(memory.Read8(0x06000043) == 'A');
    assert(memory.Read8(0x0600004b) == 'C');
    assert(!memory.DecompressRomToMemory(16,0x06017ffc,Compression::Lz77,128));
    assert(!memory.DecompressRomToMemory(16,0x08000000,Compression::Lz77,128));
    assert(!memory.DecompressRomToMemory(16,0x02000000,Compression::Lz77,10));
    assert(memory.Read8(0x02000000)==0);
    assert(!memory.DecompressRomToMemory(63,0x02000000,Compression::Lz77,128));
}
int main() {
    testLz77();
    testRle();
    testRomToVideo();
    std::cout << "GBA BIOS LZ77/RLE and ROM-to-native-RAM tests OK\n";
}
