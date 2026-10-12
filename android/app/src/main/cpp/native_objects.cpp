#include "native_objects.h"
#include "object_catalog.h"
#include <algorithm>

namespace wl4 {
namespace {
uint16_t le16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t le32(const uint8_t* p){return le16(p)|(uint32_t(le16(p+2))<<16);}
void put16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
uint32_t sequence(uint8_t id,bool alternate){
    using namespace object_catalog;
    if(id<4){
        constexpr uint32_t closed[]={sBoxJewelPieceNEClosedOam,sBoxJewelPieceSEClosedOam,sBoxJewelPieceSWClosedOam,sBoxJewelPieceNWClosedOam};
        return alternate ? sBoxJewelPieceOpenedOam : closed[id];
    }
    switch(id){
        case 4:return alternate ? sBoxCDOpenedOam : sBoxCDClosedOam;
        case 5:return alternate ? sBoxHeartRefillOpenedOam : sBoxHeartRefillClosedOam;
        case 6:return sDiamondOamData;
        case 7:return alternate ? sEscapeSwitchPressedOam : sEscapeSwitchIdleOam;
        case 8:return sKeyzerIdleOam;
        case 13:return sLittleHeartOamData;
        case 17:case 18:case 19:return sSpearMaskWalkingOam;
        case 21:return sRockOamData;
        case 23:return sKaentsuboIdleOam;
        case 25:return sTotsumenOam_83BB0E8;
        case 41:return sVortexSmallPartOam;
        case 42:case 43:return alternate ? sMarumenStunnedOam : sMarumenIdleOam;
        default:return 0;
    }
}
}

bool NativeObjects::Animation(const NativeMemory& memory,uint32_t address,std::vector<ObjectFrame>* frames){
    for(uint32_t i=0;i<128;++i){
        const auto* entry=memory.RomRange(address+i*8,8);
        if(!entry)return false;
        if(!entry[4])return !frames->empty();
        const auto* data=memory.RomRange(le32(entry),2);
        if(!data)return false;
        const size_t count=le16(data);
        if(!count || count>64)return false;
        data=memory.RomRange(le32(entry),2+count*6);
        if(!data)return false;
        ObjectFrame frame;frame.duration=entry[4];
        for(size_t j=0;j<count;++j){
            const uint16_t a0=le16(data+2+j*6),a1=le16(data+4+j*6),a2=le16(data+6+j*6);
            if((a0&0x2300) || (a0&0xc000)==0xc000)return false;
            frame.pieces.push_back({{a0,a1,a2}});
        }
        frames->push_back(std::move(frame));
    }
    return false;
}

bool NativeObjects::Load(const NativeMemory& memory,const RoomAssets& room,uint8_t group,std::string* error){
    NativeObjects next;
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!room.SpriteAddress()){*this=std::move(next);return true;} // Authored test room, no objects.
    if(group>=91)return fail("Invalid object graphics group");
    const auto* groupPointer=memory.RomRange(0x0878ef78+group*4u,4);
    if(!groupPointer)return fail("Missing object graphics list");
    std::vector<std::array<uint8_t,2>> list;
    for(uint32_t i=0;i<32;++i){
        const auto* item=memory.RomRange(le32(groupPointer)+i*2,2);
        if(!item)return fail("Object graphics list outside ROM");
        if(!item[0])break;
        list.push_back({{item[0],item[1]}});
        if(i==31)return fail("Unterminated object graphics list");
    }
    const auto copy=[&](uint32_t address,size_t bytes,uint8_t* destination,size_t capacity){
        const auto* source=memory.RomRange(address,bytes);
        if(!source || bytes>capacity)return false;
        std::copy_n(source,bytes,destination);return true;
    };
    if(!copy(0x08400ae8,0x3000,next.graphics_.data(),0x3000) ||
       !copy(0x08400a68,0x80,next.palettes_.data(),0x80))return fail("Common object artwork outside ROM");
    const auto* commonGfx=memory.RomRange(0x0878ebf0,4);
    const auto* commonPalette=memory.RomRange(0x0878edb4,4);
    if(!commonGfx || !commonPalette || !copy(le32(commonGfx),0x800,next.graphics_.data()+0x6800,0x800) ||
       !copy(le32(commonPalette),32,next.palettes_.data()+0x160,32))return fail("Common enemy artwork outside ROM");
    uint8_t previousSlot=255;
    for(const auto& item:list){
        if(item[0]<16)continue;
        if(item[1]==8 || item[1]==previousSlot)continue;
        previousSlot=item[1];
        if(item[1]>7)return fail("Invalid enemy graphics slot");
        const uint32_t index=item[0]-16;
        const auto* gfx=memory.RomRange(0x0878ebf0+index*4,4);
        const auto* size=memory.RomRange(0x083b2c90+index*4,4);
        const auto* palette=memory.RomRange(0x0878edb4+index*4,4);
        if(!gfx || !size || !palette)return fail("Missing enemy artwork");
        const uint32_t bytes=le32(size),offset=0x3000+item[1]*0x800u,palOffset=0x80+item[1]*32u;
        if(bytes>0x4000 || bytes%0x800 || offset>next.graphics_.size() || palOffset>next.palettes_.size() ||
           !copy(le32(gfx),bytes,next.graphics_.data()+offset,next.graphics_.size()-offset) ||
           !copy(le32(palette),(bytes/0x800)*32,next.palettes_.data()+palOffset,next.palettes_.size()-palOffset))
            return fail("Invalid enemy artwork range");
    }
    bool terminated=false;
    for(uint32_t i=0;i<64;++i){
        const auto* data=memory.RomRange(room.SpriteAddress()+i*3,3);
        if(!data)return fail("Room objects outside ROM");
        if(data[0]==255){terminated=true;break;}
        if(!data[2])return fail("Invalid local object id");
        RoomObject object;object.slot=uint8_t(i);object.x=data[1]*64+32;object.y=(data[0]+1)*64;
        if(data[2]<=16)object.id=data[2]-1;
        else{
            const size_t index=data[2]-17;
            if(index>=list.size())return fail("Local object id outside graphics list");
            object.id=list[index][0];object.gfxSlot=list[index][1]&7;
        }
        if(object.id==8){object.y-=64;object.x-=32;}
        if(object.id==6){object.y-=4;object.x+=32;}
        object.originX=object.x;object.originY=object.y;
        const uint32_t animation=sequence(object.id,false);
        if(!animation){++next.unsupported_;continue;}
        if(!Animation(memory,animation,&object.animation) || !Animation(memory,sequence(object.id,true),&object.alternate))
            return fail("Invalid object animation");
        next.objects_.push_back(std::move(object));
    }
    if(!terminated)return fail("Unterminated room object list");
    *this=std::move(next);return true;
}

void NativeObjects::Animate(RoomObject* object){
    if(!object || !object->active)return;
    const auto& sequence=object->opened || object->stunned ? object->alternate : object->animation;
    object->frame%=sequence.size();
    if(++object->timer>=sequence[object->frame].duration){object->timer=0;object->frame=(object->frame+1)%sequence.size();}
}

void NativeObjects::Draw(int cameraX,int cameraY,size_t first,VideoState* video) const{
    if(!video)return;
    std::copy(graphics_.begin(),graphics_.end(),video->vram.begin()+0x11000);
    std::copy(palettes_.begin(),palettes_.end(),video->palette.begin()+0x280);
    size_t index=first;
    for(const auto& object:objects_){
        if(!object.active)continue;
        const int x=object.x/4-cameraX,y=object.y/4-cameraY;
        if(x<-64 || x>304 || y<-64 || y>224)continue;
        const auto& sequence=object.opened || object.stunned ? object.alternate : object.animation;
        const auto& frame=sequence[object.frame%sequence.size()];
        for(const auto& piece:frame.pieces){
            if(index>=128)return;
            int ox=piece[1]&511;if(ox>=256)ox-=512;
            const int oy=int8_t(piece[0]&255);
            uint16_t a1=piece[1]&0xfe00,a2=piece[2];
            if(object.right){
                constexpr int widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
                const int width=widths[piece[0]>>14][piece[1]>>14];
                ox=-ox-width;a1^=0x1000;
            }
            if(object.id>=16){
                a2=uint16_t(a2+object.gfxSlot*64+(object.gfxSlot<<12));
                if(object.id==43)a2=uint16_t(a2+0x1000);
                if(object.id==18)a2=uint16_t(a2+0x1000);
                if(object.id==19)a2=uint16_t(a2+0x2000);
            }
            // OAM is the original 2D tile layout; world positions are quarter pixels.
            put16(video->oam.data()+index*8,uint16_t((piece[0]&0xff00)|((y+oy)&255)));
            put16(video->oam.data()+index*8+2,uint16_t(a1|((x+ox)&511)));
            put16(video->oam.data()+index*8+4,uint16_t((a2&~0xc00u)|0x400));
            ++index;
        }
    }
}
} // namespace wl4
