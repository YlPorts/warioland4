#pragma once
#include "room_assets.h"

namespace wl4 {
struct ObjectFrame {
    uint8_t duration=1;
    std::vector<std::array<uint16_t,3>> pieces;
};
struct RoomObject {
    uint8_t id=0,slot=0,gfxSlot=0;
    int x=0,y=0,originX=0,originY=0,timer=0,stunned=0;
    size_t frame=0;
    bool active=true,opened=false,right=false;
    std::vector<ObjectFrame> animation,alternate;
};
// Portable object artwork and a bounded subset of interactions. The original
// AI for transformations, projectiles and bosses is not implemented here.
class NativeObjects {
public:
    bool Load(const NativeMemory& memory,const RoomAssets& room,uint8_t group,std::string* error);
    void Draw(int cameraX,int cameraY,size_t firstOam,VideoState* video) const;
    std::vector<RoomObject>& Objects(){return objects_;}
    const std::vector<RoomObject>& Objects() const{return objects_;}
    unsigned Unsupported() const{return unsupported_;}
    static void Animate(RoomObject* object);
private:
    static bool Animation(const NativeMemory& memory,uint32_t address,std::vector<ObjectFrame>* frames);
    std::vector<RoomObject> objects_;
    std::array<uint8_t,0x7000> graphics_{};
    std::array<uint8_t,0x180> palettes_{};
    unsigned unsupported_=0;
};
} // namespace wl4
