#include "../app/src/main/cpp/m4a_audio.h"
#include <cassert>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <iostream>
#include <cmath>

static std::vector<uint8_t> Fixture(){
    std::vector<uint8_t> rom(0x100000,0);
    const auto put32=[&](size_t at,uint32_t v){for(int i=0;i<4;++i)rom[at+i]=uint8_t(v>>(i*8));};
    put32(0x98030,0x080a0000);
    rom[0xa0000]=1;put32(0xa0004,0x080a0100);put32(0xa0008,0x080a0200);
    rom[0xa0101]=60;put32(0xa0104,0x080a0300);
    for(int i=8;i<11;++i) rom[0xa0100+i]=255;
    rom[0xa010b]=128;
    const uint8_t commands[]={0xbb,75,0xbd,0,0xbe,100,0xbf,64,0xe7,60,100,0x98,0xb2,8,2,10,8};
    std::copy(std::begin(commands),std::end(commands),rom.begin()+0xa0200);
    rom[0xa0303]=0x40;put32(0xa0304,8000*1024);put32(0xa0308,0);put32(0xa030c,16);
    for(int i=0;i<16;++i)rom[0xa0310+i]=uint8_t(int8_t((i<8 ? i : 15-i)*30-105));
    return rom;
}

int main(int argc,char** argv){
    wl4::NativeMemory memory;
    assert(memory.LoadRom(Fixture()));
    wl4::M4aAudio audio;
    assert(audio.Play(memory,1,true));
    std::vector<int16_t> samples(44100*2);
    audio.Render(memory,samples.data(),44100);
    long long energy=0;for(int16_t sample:samples)energy+=sample*sample;
    assert(energy>1000000 && audio.RejectedTracks()==0);
    std::fill(samples.begin(),samples.end(),int16_t(123));
    audio.Render(memory,samples.data(),44100,true);
    assert(std::all_of(samples.begin(),samples.end(),[](int16_t value){return value==0;}));
    auto broken=Fixture();broken[0xa0308]=16;assert(memory.LoadRom(std::move(broken)));
    audio.Reset();assert(audio.Play(memory,1,true));audio.Render(memory,samples.data(),44100);
    assert(audio.RejectedTracks()==1);
    assert(!audio.Play(memory,786,true)); // Missing header fails cleanly.
    std::cout<<"Native PCM sequence, loop, envelope, pause and malformed sample tests passed\n";
    if(argc<2)return 0;
    std::ifstream in(argv[1],std::ios::binary);assert(in);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
    assert(memory.LoadRom(std::move(rom)));audio.Reset();
    assert(audio.Play(memory,672,true));
    samples.resize(44100*2*10);
    audio.Render(memory,samples.data(),44100*10);
    energy=0;for(int16_t sample:samples)energy+=sample*sample;
    std::cout<<"Original entry-passage BGM: RMS="<<std::sqrt(double(energy)/samples.size())
             <<", rejected tracks="<<audio.RejectedTracks()<<'\n';
    assert(energy>1000000 && audio.RejectedTracks()==0);
    if(argc>2){
        std::ofstream out(argv[2],std::ios::binary);
        const uint32_t bytes=uint32_t(samples.size()*2),riff=bytes+36,rate=44100,byterate=rate*4;
        const uint16_t format=1,channels=2,align=4,bits=16;
        const uint32_t fmtSize=16;
        out.write("RIFF",4);out.write(reinterpret_cast<const char*>(&riff),4);out.write("WAVEfmt ",8);
        out.write(reinterpret_cast<const char*>(&fmtSize),4);out.write(reinterpret_cast<const char*>(&format),2);
        out.write(reinterpret_cast<const char*>(&channels),2);out.write(reinterpret_cast<const char*>(&rate),4);
        out.write(reinterpret_cast<const char*>(&byterate),4);out.write(reinterpret_cast<const char*>(&align),2);
        out.write(reinterpret_cast<const char*>(&bits),2);out.write("data",4);out.write(reinterpret_cast<const char*>(&bytes),4);
        out.write(reinterpret_cast<const char*>(samples.data()),bytes);
    }
    for(uint16_t id:{uint16_t(681),uint16_t(13),uint16_t(19),uint16_t(30),uint16_t(217),uint16_t(220),uint16_t(309),uint16_t(314),uint16_t(320)}){
        audio.Reset();assert(audio.Play(memory,id,true));audio.Render(memory,samples.data(),44100*3);
        std::cout<<"Song "<<id<<" rejected tracks="<<audio.RejectedTracks()<<'\n';
        assert(audio.RejectedTracks()==0);
    }
}
