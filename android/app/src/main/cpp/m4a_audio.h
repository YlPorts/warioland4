#pragma once
#include "native_memory.h"
#include <array>
#include <cstdint>
#include <vector>

namespace wl4 {
// Native interpretation of music commands, not ARM/GBA CPU instructions.
// Reads the game's existing song/voice/sample data via checked ROM ranges.
class M4aAudio {
public:
    static constexpr int SampleRate=44100;
    bool Play(const NativeMemory& memory,uint16_t song,bool background);
    void Render(const NativeMemory& memory,int16_t* stereo,size_t frames,bool paused=false);
    void Reset();
    unsigned RejectedTracks() const {return rejected_;}
private:
    struct Track {
        uint32_t pc=0,voiceGroup=0;
        std::array<uint32_t,3> stack{};
        uint8_t depth=0,wait=0,status=0,voice=0,key=60,velocity=100,volume=100,pan=64;
        int keyShift=0,bend=0,bendRange=2,tune=0;
        uint8_t repeats=0;
        bool active=false;
    };
    struct Song {
        std::array<Track,16> tracks{};
        std::array<uint8_t,256> variables{};
        unsigned count=0,tempo=150;
        double clock=0;
        bool active=false,background=false;
    };
    struct Voice {
        bool active=false,loop=false,releasing=false;
        uint8_t type=0,duty=2,attack=255,decay=255,sustain=255,release=128,envelope=0;
        uint32_t sample=0,length=0,loopStart=0,lfsr=0x7fff;
        int owner=0,track=0,key=60,gate=0;
        double cursor=0,step=0,phase=0,frequency=440,amplitude=1,left=0,right=0;
    };
    void Tick(const NativeMemory& memory,size_t song);
    bool Note(const NativeMemory& memory,size_t song,size_t track,int duration);
    std::array<Song,5> songs_{};
    std::array<Voice,40> voices_{};
    double envelopeClock_=0;
    unsigned rejected_=0;
};
} // namespace wl4
