#include "native_game.h"
#include <algorithm>
#include <cstring>
extern "C" {
#include "wl4_host_game.h"
}

namespace wl4 {
namespace {
uint32_t checksum(const uint8_t* bytes,size_t count){
    uint32_t crc=0xffffffff;
    for(size_t i=0;i<count;++i){crc^=bytes[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1) ? 0xedb88320 : 0);}
    return ~crc;
}
void append(std::vector<uint8_t>* bytes,uint32_t word){for(int i=0;i<4;++i)bytes->push_back(uint8_t(word>>(i*8)));}
}

std::vector<uint8_t> NativeGame::Save(const NativeMemory& memory) const{
    std::vector<uint8_t> data{'W','L','4','N','A','T','1','0'};
    const uint64_t identity=memory.RomFingerprint();
    append(&data,uint32_t(identity));append(&data,uint32_t(identity>>32));
    for(uint32_t value:{uint32_t(checkpoint_),uint32_t(player_.x),uint32_t(player_.y),uint32_t(health_),
                       score_,jewels_,uint32_t(keyzer_),uint32_t(completed_),uint32_t(escapeFrames_),uint32_t(player_.right)})append(&data,value);
    for(uint8_t state:gSwitchStates)append(&data,state);
    append(&data,uint32_t(broken_.size()));append(&data,uint32_t(objectStates_.size()));
    for(const auto& tile:broken_)for(int value:tile)append(&data,uint32_t(value));
    for(const auto& object:objectStates_)for(uint8_t value:object)append(&data,value);
    append(&data,checksum(data.data(),data.size()));
    return data;
}

bool NativeGame::Restore(const NativeMemory& memory,const std::vector<uint8_t>& data,std::string* error){
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(data.size()<88 || data.size()>65536 || data.size()%4 || std::memcmp(data.data(),"WL4NAT10",8))return fail("Invalid native progress file");
    const auto word=[&](size_t offset){return uint32_t(data[offset])|(uint32_t(data[offset+1])<<8)|(uint32_t(data[offset+2])<<16)|(uint32_t(data[offset+3])<<24);};
    if(checksum(data.data(),data.size()-4)!=word(data.size()-4))return fail("Corrupt native progress checksum");
    const uint64_t identity=word(8)|(uint64_t(word(12))<<32);
    if(identity!=memory.RomFingerprint())return fail("Progress belongs to another ROM");
    const uint32_t entrance=word(16),x=word(20),y=word(24),health=word(28),score=word(32),jewels=word(36),
        keyzer=word(40),completed=word(44),escape=word(48),right=word(52),broken=word(76),objects=word(80);
    if(health<1||health>8||score>10000000||jewels>15||keyzer>1||completed>1||right>1||escape>60*600 || broken>2048||objects>832 ||
       data.size()!=88u+12u*(broken+objects))return fail("Invalid native progress fields");
    std::array<uint8_t,5> oldSwitches{};
    std::copy(std::begin(gSwitchStates),std::end(gSwitchStates),oldSwitches.begin());
    NativeGame next;
    // Start resets global block switch state; restore it on any failure.
    const auto reject=[&](const char* message){std::copy(oldSwitches.begin(),oldSwitches.end(),std::begin(gSwitchStates));return fail(message);};
    if(!next.Start(memory,error))return reject("Unable to load progress stage");
    if(entrance>=next.entrances_.size())return reject("Invalid progress entrance");
    size_t cursor=84;
    for(uint32_t i=0;i<broken;++i){
        const uint32_t room=word(cursor),col=word(cursor+4),row=word(cursor+8);cursor+=12;
        if(room>=next.room_.RoomCount() || col>=255 || row>=255)return reject("Invalid broken block record");
        next.broken_.push_back({{int(room),int(col),int(row)}});
    }
    for(uint32_t i=0;i<objects;++i){
        const uint32_t room=word(cursor),slot=word(cursor+4),state=word(cursor+8);cursor+=12;
        if(room>=next.room_.RoomCount()||slot>=64||state<1||state>3)return reject("Invalid object progress record");
        next.objectStates_.push_back({{uint8_t(room),uint8_t(slot),uint8_t(state)}});
    }
    for(size_t i=0;i<5;++i){const uint32_t state=word(56+i*4);if(state>3)return reject("Invalid switch progress");gSwitchStates[i]=uint8_t(state);}
    if(!next.Enter(memory,entrance,error))return reject("Unable to load saved room");
    if(x<28||x>=uint32_t(next.room_.PixelWidth()*4-28)||y<128||y>=uint32_t(next.room_.PixelHeight()*4+128))return reject("Invalid saved player position");
    next.Place(int(x),int(y));next.player_.right=right!=0;
    next.health_=int(health);next.score_=score;next.jewels_=jewels;next.keyzer_=keyzer!=0;next.completed_=completed!=0;next.escapeFrames_=int(escape);
    if(escape)next.music_=681;
    *this=std::move(next);if(error)error->clear();return true;
}
} // namespace wl4
