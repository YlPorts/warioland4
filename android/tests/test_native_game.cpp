#include "../app/src/main/cpp/native_game.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include "synthetic_game_rom.h"
extern "C" {
#include "../app/src/main/cpp/wl4_host_game.h"
}

int main(int argc,char** argv) {
    constexpr int solid=0x01000000;
    assert(wl4HostBlockCollision(0,0,0)==0);
    assert(wl4HostBlockCollision(1,0,0)==(solid|1));
    assert(!(wl4HostBlockCollision(2,48,12)&solid));
    assert(wl4HostBlockCollision(2,12,48)&solid);
    assert(!(wl4HostBlockCollision(3,12,12)&solid));
    assert(wl4HostBlockCollision(3,48,48)&solid);
    assert(!(wl4HostBlockCollision(4,48,12)&solid));
    assert(wl4HostBlockCollision(4,48,24)&solid);
    assert(!(wl4HostBlockCollision(5,32,40)&solid));
    assert(wl4HostBlockCollision(5,32,48)&solid);
    assert(!(wl4HostBlockCollision(6,32,40)&solid));
    assert(wl4HostBlockCollision(6,32,48)&solid);
    assert(!(wl4HostBlockCollision(7,16,12)&solid));
    assert(wl4HostBlockCollision(7,16,24)&solid);
    assert(wl4HostBlockCollision(12,32,32)==12);
    std::cout<<"Original solid, one-way marker and six slope kernels passed\n";
    const bool synthetic=argc<2;
    std::vector<uint8_t> rom;
    if(synthetic) rom=SyntheticGameRom();
    else {
        std::ifstream input(argv[1],std::ios::binary);
        assert(input);
        rom.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
    }
    wl4::NativeMemory memory;
    assert(memory.LoadRom(std::move(rom)));
    wl4::NativeGame game;
    std::string error;
    if(!game.Start(memory,&error)) {std::cerr<<error<<'\n';return 1;}
    for(int i=0;i<120;++i) game.Tick(memory,0);
    std::cout<<"Spawn settled at "<<game.Player().x/4<<','<<game.Player().y/4
             <<" grounded="<<game.Player().grounded<<'\n';
    assert(game.Player().grounded);
    const int y=game.Player().y;
    game.Tick(memory,1);
    for(int i=0;i<15;++i) game.Tick(memory,1);
    std::cout<<"Jump apex test y="<<game.Player().y/4<<'\n';
    assert(game.Player().y<y-32);
    for(int i=0;i<150;++i) game.Tick(memory,0);
    assert(game.Player().grounded);
    const int x=game.Player().x;
    for(int i=0;i<35;++i) game.Tick(memory,16);
    assert(game.Player().x>x+32);
    game.Tick(memory,8);
    assert(game.Paused());
    const auto paused=game.Player();
    for(int i=0;i<30;++i) game.Tick(memory,17);
    assert(game.Player().x==paused.x && game.Player().y==paused.y);
    game.Tick(memory,0);game.Tick(memory,8);game.Tick(memory,0);
    assert(!game.Paused());
    for(int i=0;i<300;++i) game.Tick(memory,272);
    assert(game.Player().x>=28 && game.Player().x<game.Room().PixelWidth()*4);
    if(synthetic) {
        // A held run cannot tunnel through the full-height wall.
        assert(game.Player().x<=12*64-29);
        assert(game.Enter(memory,0,&error));
        for(int i=0;i<100;++i) game.Tick(memory,0);
        game.Place(10*64-29,10*64-1);
        game.Tick(memory,2);
        for(int i=0;i<20;++i)game.Tick(memory,18);
        assert(game.BrokenTiles()==1);
        assert(game.Room().CollisionAt(1,10,9)==0);
        assert(game.Enter(memory,0,&error));
        assert(game.Room().CollisionAt(1,10,9)==0); // Break persists across doors.
        for(int i=0;i<25;++i) game.Tick(memory,0);
        game.Place(19*64+8,10*64-1);
        game.Tick(memory,0);
        assert(game.Room().RoomId()==1); // Actual runtime portal, not Enter().
        std::cout<<"Wall sweep, breakable tile persistence and runtime room transition passed\n";
    }
    // Every real entry is relocatable using bounded GBA door records.
    for(size_t i=0;i<game.Entrances().size();++i) {
        assert(game.Enter(memory,i,&error));
        for(int frame=0;frame<15;++frame) game.Tick(memory,(frame&1) ? 16 : 32);
        wl4::VideoState video;
        game.BuildVideo(&video);
        std::vector<uint32_t> pixels;
        wl4::GbaVideo().Render(video,&pixels);
        assert(pixels.size()==240*160 && (video.dispcnt&0x1000));
    }
    assert(game.Enter(memory,0,&error));
    for(int i=0;i<90;++i) game.Tick(memory,0);
    wl4::VideoState frame;
    game.BuildVideo(&frame);
    std::vector<uint32_t> pixels;
    wl4::GbaVideo().Render(frame,&pixels);
    if(argc>2) {
        std::ofstream image(argv[2],std::ios::binary);
        image<<"P6\n240 160\n255\n";
        for(uint32_t color:pixels) {
            const char rgb[]={char(color),char(color>>8),char(color>>16)};
            image.write(rgb,3);
        }
    }
    std::cout<<"Original Wario animations, gravity, jump, landing, movement, pause and "
             <<game.Entrances().size()<<" real entrances passed\n";
}
