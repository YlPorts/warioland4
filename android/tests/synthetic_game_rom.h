#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../app/src/main/cpp/object_catalog.h"

// Hand-authored test fixture. Contains no bytes extracted from the game.
// It uses the documented table/room ABI to exercise gameplay in CI.
inline std::vector<uint8_t> SyntheticGameRom(bool objects=false) {
    std::vector<uint8_t> rom(0x800000,0);
    const auto put16=[&](size_t at,uint16_t v){rom[at]=uint8_t(v);rom[at+1]=uint8_t(v>>8);};
    const auto put32=[&](size_t at,uint32_t v){put16(at,uint16_t(v));put16(at+2,uint16_t(v>>16));};
    std::copy_n("AWAE",4,rom.begin()+0xac);
    rom[0x639068]=0;rom[0x639069]=2;
    put32(0x78f280,0x08101000);
    put32(0x78f21c,0x08102000);
    // Two rooms with a floor, wall, one-way platform and breakable block.
    for(int room=0;room<2;++room) {
        const size_t header=0x101000+room*44;
        rom[header+2]=16;rom[header+4]=32;
        put32(header+8,0x08103000);
        put32(header+12,0x08104000);
        put32(header+16,0x08103000);
        put32(header+20,0x08103000);
    }
    const uint8_t entries[][12]={
        {1,0,5,5,8,8,0,0,0,0,0,0},
        {2,0,19,19,8,9,2,224,0,0,0,0},
        {2,1,0,0,8,9,1,32,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0}};
    std::copy_n(&entries[0][0],sizeof(entries),rom.begin()+0x102000);
    // Screen-size 0, 1024 transparent tile entries, two 16-bit RLE planes.
    const uint8_t background[]={0,2,0x84,0,0x40,0,0,2,0x84,0,0,0,0};
    std::copy_n(background,sizeof(background),rom.begin()+0x103000);
    std::vector<uint8_t> map(20*12,0);
    for(int col=0;col<20;++col) map[10*20+col]=1;
    for(int row=5;row<10;++row) map[row*20+12]=1;
    for(int col=4;col<=7;++col) map[7*20+col]=2;
    map[9*20+10]=3;
    size_t offset=0x104000;
    rom[offset++]=20;rom[offset++]=12;
    rom[offset++]=1;
    for(size_t cursor=0;cursor<map.size();) {
        const size_t run=std::min<size_t>(127,map.size()-cursor);
        rom[offset++]=uint8_t(run);
        for(size_t i=0;i<run;++i)rom[offset++]=map[cursor++];
    }
    rom[offset++]=0;
    rom[offset++]=2;rom[offset++]=0x80;rom[offset++]=240;rom[offset++]=0;rom[offset++]=0;rom[offset++]=0;
    const size_t tileset=0x3f2298;
    put32(tileset,0x08108000);put16(tileset+4,32);
    put32(tileset+8,0x08109000);
    put32(tileset+12,0x0810a000);put16(tileset+16,32);
    put32(tileset+20,0x0810b000);put32(tileset+24,0x0810c000);put32(tileset+28,0x0810d000);
    std::fill_n(rom.begin()+0x108000,32,uint8_t(0x11));
    put16(0x109002,0x7fff);
    for(size_t id=0;id<4;++id)for(size_t k=0;k<4;++k)put16(0x10b000+id*8+k*2,id ? 0x41 : 0x40);
    rom[0x10c001]=1;rom[0x10c002]=12;rom[0x10c003]=1;
    put16(0x10d002,2);put16(0x10d006,11);
    // A simple authored 8x8 OBJ with one frame and a terminator.
    put32(0x105000,0x08106000);put32(0x105004,0x08107000);rom[0x105008]=4;
    rom[0x106000]=1;std::fill_n(rom.begin()+0x106002,32,uint8_t(0x11));
    put16(0x107000,1);put16(0x107002,248);put16(0x107004,508);put16(0x107006,0);
    put16(0x2ddda2,0x03ff);put16(0x2ddde2,0x03ff);
    put16(0x2deb18,uint16_t(-28));put16(0x2deb1a,uint16_t(-126));put16(0x2deb1c,28);
    for(size_t pose=0;pose<78;++pose)for(size_t dir=0;dir<2;++dir)put32(0x2dd110+pose*8+dir*4,0x08105000);
    for(size_t i=0;i<4;++i)put32(0x2dd3f8+i*4,0x08105000);
    if(objects){
        put32(0x101000+32,0x08111000);
        const uint8_t records[]={9,4,9, 9,6,7, 9,8,8, 9,10,1, 9,14,17, 9,18,18, 255,0,0};
        std::copy_n(records,sizeof(records),rom.begin()+0x111000);
        put32(0x78ef78,0x08112000);rom[0x112000]=42;rom[0x112001]=0;
        rom[0x112002]=41;rom[0x112003]=1;
        for(unsigned id:{0u,25u,26u}){
            put32(0x78ebf0+id*4,0x08114000);put32(0x78edb4+id*4,0x08116000);
            put32(0x3b2c90+id*4,0x800);
        }
        std::fill_n(rom.begin()+0x114000,0x800,uint8_t(0x11));put16(0x116002,0x7fff);
        put16(0x113000,1);put16(0x113002,248);put16(0x113004,508);put16(0x113006,128|(4<<12));
        using namespace wl4::object_catalog;
        for(uint32_t address:{sKeyzerIdleOam,sDiamondOamData,sEscapeSwitchIdleOam,sEscapeSwitchPressedOam,
                              sBoxJewelPieceNEClosedOam,sBoxJewelPieceOpenedOam,sMarumenIdleOam,sMarumenStunnedOam,sVortexSmallPartOam}){
            const size_t at=address-0x08000000;
            put32(at,0x08113000);rom[at+4]=4;
            std::fill_n(rom.begin()+at+8,8,0);
        }
    }
    return rom;
}
