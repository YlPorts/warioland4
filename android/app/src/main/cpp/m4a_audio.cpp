#include "m4a_audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace wl4 {
namespace {
constexpr double FrameRate=16777216.0/280896.0;
constexpr uint8_t clocks[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    28,30,32,36,40,42,44,48,52,54,56,60,64,66,68,72,76,78,80,84,88,90,92,96};
uint32_t le32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
}

void M4aAudio::Reset(){songs_={};voices_={};envelopeClock_=0;rejected_=0;}

bool M4aAudio::Play(const NativeMemory& memory,uint16_t id,bool background){
    if(id>=787)return false;
    const auto* table=memory.RomRange(0x08098028+id*8u,8);
    const auto* header=table ? memory.RomRange(le32(table),8) : nullptr;
    if(!header || !header[0] || header[0]>16)return false;
    const auto* parts=memory.RomRange(le32(table)+8,header[0]*4u);
    if(!parts || !memory.RomRange(le32(header+4),12))return false;
    size_t slot=0;
    if(!background){slot=1;while(slot<4 && songs_[slot].active)++slot;}
    Song song; song.background=background;song.active=true;song.count=header[0];
    for(size_t i=0;i<song.count;++i){
        const uint32_t pc=le32(parts+i*4);
        if(!memory.RomRange(pc,1))return false;
        song.tracks[i].pc=pc;song.tracks[i].voiceGroup=le32(header+4);song.tracks[i].active=true;
    }
    songs_[slot]=song;
    for(auto& voice:voices_)if(voice.owner==int(slot))voice.active=false;
    return true;
}

bool M4aAudio::Note(const NativeMemory& memory,size_t owner,size_t index,int duration){
    auto& track=songs_[owner].tracks[index];
    const auto* tone=memory.RomRange(track.voiceGroup+track.voice*12u,12);
    if(!tone)return false;
    int key=track.key;
    for(int nesting=0;nesting<3 && (tone[0]&0xc0);++nesting){
        const bool rhythm=(tone[0]&0x80)!=0;
        const uint32_t bank=le32(tone+4);
        int selection=key;
        if(tone[0]&0x40){const auto* map=memory.RomRange(le32(tone+8)+key,1);if(!map)return false;selection=*map;}
        const auto* selected=memory.RomRange(bank+uint32_t(selection)*12u,12);
        if(!selected)return false;
        tone=selected;
        // Only rhythm tones replace the played key; split instruments keep it.
        if(rhythm)key=tone[1];
    }
    if(tone[0]&0xc0)return false;
    key=std::clamp(key+track.keyShift,0,127);
    auto available=std::find_if(voices_.begin(),voices_.end(),[](const Voice& voice){return !voice.active;});
    if(available==voices_.end())available=voices_.begin()+owner*6;
    Voice voice;
    voice.active=true;voice.owner=int(owner);voice.track=int(index);voice.key=track.key;
    voice.gate=duration;voice.type=tone[0]&7;voice.duty=uint8_t(le32(tone+4)&3);
    const double bend=track.bend*track.bendRange/64.0+track.tune/64.0;
    voice.frequency=440*std::pow(2.0,(key-69+bend)/12.0);
    const double gain=double(track.volume)*track.velocity/(127.0*127.0);
    voice.left=gain*(127-track.pan)/127.0;voice.right=gain*track.pan/127.0;
    if(voice.type==0){
        const uint32_t waveAddress=le32(tone+4);
        const auto* wave=memory.RomRange(waveAddress,16);
        if(!wave || wave[0] || wave[1])return false; // Compressed PCM is still unsupported.
        voice.length=le32(wave+12);voice.loopStart=le32(wave+8);
        if(!voice.length || voice.length>0x100000 || !memory.RomRange(waveAddress+16,voice.length))return false;
        voice.sample=waveAddress+16;voice.loop=(wave[3]&0xc0)!=0;
        if(voice.loop && voice.loopStart>=voice.length)return false;
        voice.step=double(le32(wave+4))/(1024.0*SampleRate);
        if(!(tone[0]&8))voice.step*=std::pow(2.0,(key-60+bend)/12.0);
        if(!std::isfinite(voice.step) || voice.step<=0 || voice.step>64)return false;
        voice.attack=tone[8];voice.decay=tone[9];voice.sustain=tone[10];voice.release=tone[11];
        voice.amplitude=voice.attack==255 ? 1 : 0;
        voice.envelope=voice.attack==255 ? 1 : 0;
    } else if(voice.type==3){
        voice.sample=le32(tone+4);voice.length=32;
        if(!memory.RomRange(voice.sample,16))return false;
    } else if(voice.type>4)return false;
    *available=voice;
    return true;
}

void M4aAudio::Tick(const NativeMemory& memory,size_t owner){
    auto& song=songs_[owner];
    for(auto& voice:voices_)if(voice.active && voice.owner==int(owner) && voice.gate>0 && --voice.gate==0)voice.releasing=true;
    bool any=false;
    for(size_t index=0;index<song.count;++index){
        auto& track=song.tracks[index];if(!track.active)continue;
        any=true;
        if(track.wait && --track.wait)continue;
        const auto byte=[&](uint8_t* out){const auto* p=memory.RomRange(track.pc,1);if(!p)return false;*out=*p;++track.pc;return true;};
        const auto address=[&](uint32_t* out){const auto* p=memory.RomRange(track.pc,4);if(!p)return false;*out=le32(p);track.pc+=4;return memory.RomRange(*out,1)!=nullptr;};
        bool valid=true,blocked=false;
        for(int budget=0;budget<256 && track.active && !track.wait;++budget){
            uint8_t command=0;
            const auto* next=memory.RomRange(track.pc,1);
            if(!next){valid=false;break;}
            if(*next<0x80){command=track.status;if(command<0xbd){valid=false;break;}}
            else {command=*next;++track.pc;if(command>=0xbd)track.status=command;}
            uint8_t value=0;
            uint32_t target=0;
            if(command>=0x80 && command<=0xb0){track.wait=clocks[command-0x80];continue;}
            if(command>=0xcf){
                const auto* optional=memory.RomRange(track.pc,1);
                if(optional && *optional<0x80){byte(&track.key);optional=memory.RomRange(track.pc,1);}
                if(optional && *optional<0x80){byte(&track.velocity);optional=memory.RomRange(track.pc,1);}
                int duration=command==0xcf ? -1 : clocks[command-0xcf];
                if(optional && *optional<0x80){byte(&value);duration+=value;}
                if(!Note(memory,owner,index,duration)){valid=false;break;}
                continue;
            }
            switch(command){
                case 0xb1:track.active=false;for(auto& voice:voices_)if(voice.owner==int(owner)&&voice.track==int(index))voice.releasing=true;break;
                case 0xb2:valid=address(&target);if(valid)track.pc=target;break;
                case 0xb3:valid=address(&target);if(valid && track.depth<3){track.stack[track.depth++]=track.pc;track.pc=target;}else valid=false;break;
                case 0xb4:if(track.depth)track.pc=track.stack[--track.depth];break;
                case 0xb5:valid=byte(&value)&&address(&target);if(valid){if(++track.repeats<value)track.pc=target;else track.repeats=0;}break;
                case 0xb9:{
                    uint8_t op=0,slot=0,data=0;
                    valid=byte(&op)&&byte(&slot)&&byte(&data);
                    if(!valid)break;
                    auto& variable=song.variables[slot];
                    if(op<6){
                        const uint8_t operand=op<3 ? data : song.variables[data];
                        switch(op%3){case 0:variable=operand;break;case 1:variable+=operand;break;case 2:variable-=operand;break;}
                    }else if(op<18){
                        const uint8_t operand=op<12 ? data : song.variables[data];
                        bool take=false;
                        switch((op-6)%6){case 0:take=variable==operand;break;case 1:take=variable!=operand;break;
                            case 2:take=variable>operand;break;case 3:take=variable>=operand;break;
                            case 4:take=variable<=operand;break;case 5:take=variable<operand;break;}
                        valid=address(&target);if(valid&&take)track.pc=target;
                    }
                    break;
                }
                case 0xba:valid=byte(&value);break; // track priority
                case 0xbb:valid=byte(&value);if(valid)song.tempo=std::clamp(unsigned(value)*2,1u,510u);break;
                case 0xbc:valid=byte(&value);if(valid)track.keyShift=int8_t(value);break;
                case 0xbd:valid=byte(&track.voice);break;
                case 0xbe:valid=byte(&track.volume);break;
                case 0xbf:valid=byte(&track.pan);break;
                case 0xc0:valid=byte(&value);if(valid)track.bend=int(value)-64;break;
                case 0xc1:valid=byte(&value);if(valid)track.bendRange=value;break;
                case 0xc2:case 0xc3:case 0xc4:case 0xc5:valid=byte(&value);break; // modulation parameter, not yet synthesized
                case 0xc7:valid=byte(&value);if(valid)track.tune=int(value)-64;break;
                case 0xcd:
                    target=track.key;
                    next=memory.RomRange(track.pc,1);if(next&&*next<0x80){byte(&value);target=value;}
                    for(auto& voice:voices_)if(voice.owner==int(owner)&&voice.track==int(index)&&voice.key==int(target))voice.releasing=true;
                    break;
                default:valid=false;break;
            }
            if(!valid)break;
            if(budget==255)blocked=true;
        }
        if(!valid||blocked){track.active=false;++rejected_;}
    }
    song.active=any;
}

void M4aAudio::Render(const NativeMemory& memory,int16_t* stereo,size_t frames,bool paused){
    if(!stereo)return;
    std::fill(stereo,stereo+frames*2,int16_t(0));
    if(paused)return;
    for(size_t sample=0;sample<frames;++sample){
        for(size_t i=0;i<songs_.size();++i)if(songs_[i].active){
            auto& song=songs_[i];song.clock+=FrameRate*song.tempo/(150.0*SampleRate);
            if(song.clock>=1){song.clock-=1;Tick(memory,i);}
        }
        const bool envelopeTick=(envelopeClock_+=FrameRate/SampleRate)>=1;
        if(envelopeTick)envelopeClock_-=1;
        double left=0,right=0;
        for(auto& voice:voices_){
            if(!voice.active)continue;
            if(envelopeTick){
                if(voice.releasing)voice.amplitude*=voice.release/256.0;
                else if(voice.envelope==0){
                    voice.amplitude=std::min(1.0,voice.amplitude+voice.attack/256.0);
                    if(voice.amplitude>=1)voice.envelope=1;
                }else if(voice.envelope==1){
                    voice.amplitude=std::max(voice.sustain/255.0,voice.amplitude*voice.decay/256.0);
                    if(voice.amplitude<=voice.sustain/255.0)voice.envelope=2;
                }
                if(voice.amplitude<0.0001 && voice.releasing){voice.active=false;continue;}
            }
            double value=0;
            if(voice.type==0){
                if(voice.cursor>=voice.length && voice.loop)
                    voice.cursor=voice.loopStart+std::fmod(voice.cursor-voice.loopStart,voice.length-voice.loopStart);
                if(voice.cursor>=voice.length){voice.active=false;continue;}
                const size_t index=size_t(voice.cursor);
                const auto* bytes=memory.RomRange(voice.sample+uint32_t(index),1);
                if(!bytes){voice.active=false;continue;}
                value=int8_t(*bytes)/128.0;voice.cursor+=voice.step;
            }else{
                const double old=voice.phase;
                voice.phase+=voice.frequency/SampleRate;
                voice.phase-=std::floor(voice.phase);
                if(voice.type==3){
                    const unsigned position=unsigned(voice.phase*32)&31;
                    const auto* wave=memory.RomRange(voice.sample+position/2,1);
                    if(!wave){voice.active=false;continue;}
                    const unsigned nibble=(position&1) ? *wave&15u : *wave>>4;
                    value=(int(nibble)-7.5)/8.0;
                }else if(voice.type==4){
                    if(voice.phase<old){const uint32_t bit=(voice.lfsr^(voice.lfsr>>1))&1;voice.lfsr=(voice.lfsr>>1)|(bit<<14);}
                    value=(voice.lfsr&1) ? 0.5 : -0.5;
                }else{
                    constexpr double duty[]={0.125,0.25,0.5,0.75};
                    value=voice.phase<duty[voice.duty] ? 0.6 : -0.6;
                }
            }
            left+=value*voice.amplitude*voice.left;right+=value*voice.amplitude*voice.right;
        }
        stereo[sample*2]=int16_t(std::clamp(left*6000.0,-32767.0,32767.0));
        stereo[sample*2+1]=int16_t(std::clamp(right*6000.0,-32767.0,32767.0));
    }
}
} // namespace wl4
