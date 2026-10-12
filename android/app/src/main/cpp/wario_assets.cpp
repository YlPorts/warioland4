#include "wario_assets.h"
#include <algorithm>
#include <cstring>

namespace wl4 {
namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t le32(const uint8_t* p) { return le16(p) | (uint32_t(le16(p+2)) << 16); }
void put16(uint8_t* p, uint16_t v) { p[0]=uint8_t(v); p[1]=uint8_t(v>>8); }
}

bool WarioAssets::ReadAnimation(const NativeMemory& memory, uint32_t address,
                               std::vector<WarioFrame>* output) {
    std::vector<WarioFrame> sequence;
    for (uint32_t index=0; index<128; ++index) {
        const uint64_t at=uint64_t(address)+index*12u;
        const auto* header=at<=UINT32_MAX ? memory.RomRange(uint32_t(at),12) : nullptr;
        if (!header) return false;
        if (!header[8]) { if(sequence.empty()) return false; *output=std::move(sequence); return true; }
        const uint32_t graphicsAddress=le32(header), objectsAddress=le32(header+4);
        const auto* graphics=memory.RomRange(graphicsAddress,2);
        const auto* objects=memory.RomRange(objectsAddress,2);
        if(!graphics || !objects) return false;
        const size_t upper=graphics[0]*32u, lower=graphics[1]*32u, count=le16(objects);
        if(upper>0x400 || lower>0x400 || count>64 || !count) return false;
        graphics=memory.RomRange(graphicsAddress,2+upper+lower);
        objects=memory.RomRange(objectsAddress,2+count*6);
        if(!graphics || !objects) return false;
        WarioFrame frame;
        frame.upper.assign(graphics+2,graphics+2+upper);
        frame.lower.assign(graphics+2+upper,graphics+2+upper+lower);
        frame.duration=header[8];
        for(size_t i=0;i<count;++i) {
            const auto* object=objects+2+i*6;
            const uint16_t a0=le16(object), a1=le16(object+2), a2=le16(object+4);
            if((a0&0x0300) || (a0&0xc000)==0xc000 || (a0&0x2000)) return false;
            frame.objects.push_back({{a0,a1,a2}});
        }
        sequence.push_back(std::move(frame));
    }
    return false;
}

bool WarioAssets::Load(const NativeMemory& memory, std::string* error) {
    WarioAssets next;
    const auto* normalPalette=memory.RomRange(0x082ddda0,32);
    const auto* attackPalette=memory.RomRange(0x082ddde0,96);
    if(!normalPalette || !attackPalette) {if(error)*error="Missing Wario palettes"; return false;}
    // Normal frames may reference the secondary OBJ banks during actions.
    for(size_t i=0;i<3;++i) std::copy(normalPalette,normalPalette+32,next.palette_.begin()+i*32);
    std::copy(attackPalette,attackPalette+96,next.bashPalette_.begin());
    for(uint8_t pose: {uint8_t(0),uint8_t(2),uint8_t(5),uint8_t(7),uint8_t(22),
                       uint8_t(23),uint8_t(24),uint8_t(27),uint8_t(29),uint8_t(40),uint8_t(41)}) {
        const auto* properties=memory.RomRange(0x082dd430+pose*8u,8);
        const auto* hitbox=properties && properties[0]<9 ? memory.RomRange(0x082deb18+properties[0]*8u,8) : nullptr;
        if(!hitbox) {if(error)*error="Invalid Wario hitbox"; return false;}
        for(size_t k=0;k<4;++k) next.hitboxes_[pose][k]=int16_t(le16(hitbox+k*2));
        for(uint32_t dir=0;dir<2;++dir) {
            const auto* pointer=memory.RomRange(0x082dd110+pose*8u+dir*4u,4);
            if(!pointer || !next.ReadAnimation(memory,le32(pointer),&next.animations_[pose][dir])) {
                if(error)*error="Invalid Wario pose animation "+std::to_string(pose);
                return false;
            }
        }
    }
    // DrawNormalWario selects a separate ascending/descending table for pose 4.
    next.hitboxes_[4]={{-28,-120,28,0}};
    for(uint32_t bank=0;bank<2;++bank) for(uint32_t dir=0;dir<2;++dir) {
        const auto* pointer=memory.RomRange(0x082dd3f8+bank*8u+dir*4u,4);
        if(!pointer || !next.ReadAnimation(memory,le32(pointer),&next.animations_[78+bank][dir])) {
            if(error)*error="Invalid airborne Wario animation";
            return false;
        }
    }
    *this=std::move(next);
    if(error)error->clear();
    return true;
}

const std::vector<WarioFrame>& WarioAssets::Animation(uint8_t pose, bool right, bool descending) const {
    const size_t index=pose==4 ? (descending ? 79 : 78) : pose;
    const auto& sequence=animations_.at(index).at(right ? 0 : 1);
    return sequence.empty() ? animations_[2][right ? 0 : 1] : sequence;
}

std::array<int,4> WarioAssets::Hitbox(uint8_t pose) const {
    const auto box=hitboxes_.at(pose);
    return box[1] ? box : hitboxes_[2];
}

void WarioAssets::Draw(uint8_t pose, bool right, bool descending, size_t index,
                       int x, int y, VideoState* video) const {
    if(!video) return;
    const auto& frames=Animation(pose,right,descending);
    if(frames.empty()) return;
    const auto& frame=frames[index%frames.size()];
    std::fill(video->vram.begin()+0x10000,video->vram.begin()+0x10800,0);
    std::copy(frame.upper.begin(),frame.upper.end(),video->vram.begin()+0x10000);
    std::copy(frame.lower.begin(),frame.lower.end(),video->vram.begin()+0x10400);
    const auto& palette=(pose==22 || pose==23 || pose==24) ? bashPalette_ : palette_;
    std::copy(palette.begin(),palette.end(),video->palette.begin()+0x200);
    // Original Wario graphics are uploaded in two OBJ rows, 0x400 bytes
    // apart. They require 2D OBJ mapping (row stride 32 tiles).
    video->dispcnt=uint16_t((video->dispcnt|0x1000)&~0x40u);
    // Original GameScreenDrawWario OAM offsets, signed at the source bit width.
    for(size_t i=0;i<frame.objects.size();++i) {
        const auto& object=frame.objects[i];
        const int ox=(object[1]&0x1ff)>=256 ? (object[1]&0x1ff)-512 : object[1]&0x1ff;
        const int oy=int8_t(object[0]&0xff);
        auto* destination=video->oam.data()+i*8;
        put16(destination,uint16_t((object[0]&0xff00)|((y+oy+2)&255)));
        put16(destination+2,uint16_t((object[1]&0xfe00)|((x+ox)&511)));
        put16(destination+4,uint16_t((object[2]&~0x0c00u)|0x0400));
    }
}
} // namespace wl4
