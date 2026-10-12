#pragma once
#include "room_assets.h"
#include "wario_assets.h"
#include <array>
#include <string>
#include <vector>

namespace wl4 {
struct RoomEntrance {
    uint8_t type=0, room=0, left=0, right=0, top=0, bottom=0, destination=0;
    int8_t offsetX=0, offsetY=0;
};
struct PlayerState {
    int x=0,y=0,vx=0,vy=0; // Positions: 1/4 pixel; velocities: 1/32 pixel/frame.
    uint8_t pose=4;
    bool right=true, grounded=false, climbing=false;
};

// First native playable slice: original rooms, artwork and block collision
// kernels with a portable subset of normal-Wario movement. This does not
// pretend to execute the complete original game-mode/pose/enemy logic.
class NativeGame {
public:
    bool Start(const NativeMemory& memory, std::string* error=nullptr);
    bool Enter(const NativeMemory& memory, size_t entrance, std::string* error=nullptr);
    void Tick(const NativeMemory& memory, uint16_t buttons);
    void BuildVideo(VideoState* video) const;
    const PlayerState& Player() const {return player_;}
    const RoomAssets& Room() const {return room_;}
    int CameraX() const {return cameraX_;}
    int CameraY() const {return cameraY_;}
    bool Paused() const {return paused_;}
    int BrokenTiles() const {return int(broken_.size());}
    const std::vector<RoomEntrance>& Entrances() const {return entrances_;}
    // Debug/test repositioning uses the same bounded state and collision path.
    void Place(int xQuarterPixels, int yQuarterPixels);
private:
    bool ReadEntrances(const NativeMemory& memory, std::string* error);
    bool SolidAt(int x,int y,bool oneWay,int previousFeet) const;
    bool Fits(int x,int y,int top) const;
    bool Supported(int x,int y) const;
    bool BreakAt(int x,int y);
    void Move(int dx,int dy,int top);
    void FollowCamera();
    void SetPose(uint8_t pose);
    void Animate();
    RoomAssets room_;
    WarioAssets sprites_;
    PlayerState player_;
    std::vector<RoomEntrance> entrances_;
    std::vector<std::array<int,3>> broken_;
    size_t frame_=0, checkpoint_=0;
    int frameTimer_=0, attackTimer_=0, transitionTimer_=0, cameraX_=0, cameraY_=0;
    uint16_t previousButtons_=0;
    bool paused_=false;
};
} // namespace wl4
