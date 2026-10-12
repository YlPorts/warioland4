#include "native_game.h"
#include <algorithm>
#include <utility>
extern "C" {
#include "wl4_host_game.h"
}

namespace wl4 {
namespace {
constexpr uint16_t A=1,B=2,Select=4,StartButton=8,Right=16,Left=32,Up=64,Down=128,Run=768;
int divideFloor(int value,int divisor) {return value>=0 ? value/divisor : -((-value+divisor-1)/divisor);}
uint32_t le32(const uint8_t* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
}

bool NativeGame::ReadEntrances(const NativeMemory& memory,std::string* error) {
    const auto* table=memory.RomRange(0x0878f21c+room_.RoomTableIndex()*4u,4);
    if(!table) {if(error)*error="Missing entrance table";return false;}
    const uint32_t address=le32(table);
    std::vector<RoomEntrance> entries;
    for(uint32_t i=0;i<256;++i) {
        const uint64_t at=uint64_t(address)+i*12u;
        const auto* data=at<=UINT32_MAX ? memory.RomRange(uint32_t(at),12) : nullptr;
        if(!data) {if(error)*error="Entrance outside ROM";return false;}
        if(!data[0]) {
            if(entries.empty()) return false;
            for(const auto& entry:entries) if(entry.destination>=entries.size()) return false;
            entrances_=std::move(entries); return true;
        }
        if(data[0]>5 || data[1]>=room_.RoomCount() || data[2]>data[3] || data[4]>data[5]) {
            if(error) *error="Invalid room entrance";
            return false;
        }
        entries.push_back({data[0],data[1],data[2],data[3],data[4],data[5],data[6],int8_t(data[7]),int8_t(data[8]),data[9],uint16_t(data[10]|(data[11]<<8))});
    }
    if(error)*error="Unterminated entrance table";
    return false;
}

bool NativeGame::Start(const NativeMemory& memory,std::string* error) {
    NativeGame next;
    if(!next.sprites_.Load(memory,error) || !next.room_.Load(memory,0,0,error) ||
       !next.ReadEntrances(memory,error) || !next.Enter(memory,0,error)) return false;
    std::fill(std::begin(gSwitchStates),std::end(gSwitchStates),uint8_t(0));
    wl4HostBlockDamageTimer=0;
    *this=std::move(next);
    return true;
}

bool NativeGame::Enter(const NativeMemory& memory,size_t index,std::string* error) {
    if(index>=entrances_.size()) {if(error)*error="Invalid entrance index";return false;}
    const auto entry=entrances_[index];
    RoomAssets next;
    if(!next.Load(memory,0,entry.room,error)) return false;
    NativeObjects objects;
    if(!objects.Load(memory,next,entry.graphicsGroup,error))return false;
    for(auto& object:objects.Objects())for(const auto& state:objectStates_)if(state[0]==entry.room&&state[1]==object.slot){
        object.opened=state[2]==2;object.active=state[2]==2;
    }
    for(const auto& tile:broken_) if(tile[0]==entry.room) next.ClearTile(tile[1],tile[2]);
    room_=std::move(next);
    objects_=std::move(objects);
    if(entry.music && !escapeFrames_)music_=entry.music;
    checkpoint_=index;
    int x=entry.left*64+(entry.offsetX+8)*4;
    int y=(entry.bottom+1)*64+entry.offsetY*4-1;
    if(entry.type==3) {x=(entry.left+1)*64;y=(entry.bottom+1)*64-1;}
    Place(x,y);
    player_.right=entry.offsetX>=0;
    transitionTimer_=24;
    attackTimer_=0;
    FollowCamera();
    return true;
}

bool NativeGame::SolidAt(int x,int y,bool oneWay,int previousFeet) const {
    if(x<0 || x>=room_.PixelWidth()*4 || y<0) return true;
    if(y>=room_.PixelHeight()*4) return false;
    const int col=x/64,row=y/64;
    uint8_t type=room_.CollisionAt(1,col,row);
    if(!type) {
        const uint8_t secondary=room_.CollisionAt(2,col,row);
        if(room_.LayerParameter(2)==16 && (secondary==12 || (secondary>=38 && secondary<=45))) type=secondary;
    }
    const int collision=wl4HostBlockCollision(type,uint8_t(x&63),uint8_t(y&63));
    if(collision&0x01000000) return true;
    // Original type 12 is a one-way surface, not a solid wall or ceiling.
    return oneWay && (collision&255)==12 && previousFeet<row*64 && y>=row*64;
}

bool NativeGame::Fits(int x,int y,int top) const {
    for(int offset=top;offset<=0;offset+=8) {
        if(SolidAt(x-28,y+offset,false,y) || SolidAt(x+28,y+offset,false,y)) return false;
    }
    return !SolidAt(x-28,y,false,y) && !SolidAt(x,y,false,y) && !SolidAt(x+28,y,false,y);
}

bool NativeGame::Supported(int x,int y) const {
    for(int offset: {-24,0,24}) if(SolidAt(x+offset,y+1,true,y)) return true;
    return false;
}

void NativeGame::Place(int x,int y) {
    player_={};
    player_.x=std::clamp(x,28,std::max(28,room_.PixelWidth()*4-29));
    player_.y=std::clamp(y,128,std::max(128,room_.PixelHeight()*4-1));
    player_.grounded=Supported(player_.x,player_.y);
    player_.right=true;
    frame_=0;frameTimer_=0;
    SetPose(player_.grounded ? 2 : 4);
    FollowCamera();
}

bool NativeGame::BreakAt(int x,int y) {
    if(!attackTimer_ && player_.pose!=27) return false;
    if(x<0 || y<0) return false;
    const int col=x/64,row=y/64;
    const uint16_t attribute=room_.AttributeAt(1,col,row);
    // First-pass tile interaction subset: ordinary/fragile breakable blocks.
    // Replacement and transformation-only blocks remain handled by collision.
    const bool ordinary=attribute>=11 && attribute<=30 && (attribute-11)%5<=2;
    const bool fragile=attribute>=43 && attribute<=45;
    if(!ordinary && !fragile) return false;
    if(!room_.ClearTile(col,row)) return false;
    const std::array<int,3> key={{room_.RoomId(),col,row}};
    if(std::find(broken_.begin(),broken_.end(),key)==broken_.end()) broken_.push_back(key);
    return true;
}

void NativeGame::Move(int dx,int dy,int top) {
    const bool wasGrounded=player_.grounded;
    const int direction=dx>=0 ? 1 : -1;
    for(int step=0;step<std::abs(dx);++step) {
        const int next=player_.x+direction;
        const int edge=next+direction*28;
        for(int offset=top;offset<=0;offset+=8) BreakAt(edge,player_.y+offset);
        if(Fits(next,player_.y,top)) {player_.x=next;continue;}
        bool climbedSlope=false;
        if(wasGrounded) {
            for(int rise=1;rise<=8;++rise) if(Fits(next,player_.y-rise,top) && Supported(next,player_.y-rise)) {
                player_.x=next;player_.y-=rise;climbedSlope=true;break;
            }
        }
        if(!climbedSlope) {player_.vx=0;attackTimer_=0;break;}
    }
    const int vertical=dy>=0 ? 1 : -1;
    player_.grounded=false;
    for(int step=0;step<std::abs(dy);++step) {
        const int next=player_.y+vertical;
        const int edge=next+(vertical<0 ? top : 0);
        bool collision=false;
        for(int offset: {-28,-16,0,16,28}) {
            BreakAt(player_.x+offset,edge);
            if(SolidAt(player_.x+offset,edge,vertical>0,player_.y)) collision=true;
        }
        if(collision) {
            player_.vy=0;
            if(vertical>0) player_.grounded=true;
            break;
        }
        player_.y=next;
    }
    if(!player_.grounded && player_.vy>=0 && Supported(player_.x,player_.y)) player_.grounded=true;
    // Follow descending slopes without floating above them; never step over a
    // cliff farther than 2 pixels when walking.
    if(wasGrounded && !player_.grounded && player_.vy>=0) {
        for(int fall=1;fall<=8;++fall) if(Fits(player_.x,player_.y+fall,top) && Supported(player_.x,player_.y+fall)) {
            player_.y+=fall;player_.grounded=true;player_.vy=0;break;
        }
    }
}

void NativeGame::SetPose(uint8_t pose) {
    if(player_.pose==pose) return;
    player_.pose=pose;frame_=0;frameTimer_=0;
}

void NativeGame::Animate() {
    const auto& sequence=sprites_.Animation(player_.pose,player_.right,player_.vy>=0);
    if(sequence.empty()) return;
    frame_%=sequence.size();
    if(++frameTimer_>=sequence[frame_].duration) {frameTimer_=0;frame_=(frame_+1)%sequence.size();}
}

void NativeGame::FollowCamera() {
    cameraX_=std::clamp(player_.x/4-120,0,std::max(0,room_.PixelWidth()-240));
    cameraY_=std::clamp(player_.y/4-100,0,std::max(0,room_.PixelHeight()-160));
}

void NativeGame::Tick(const NativeMemory& memory,uint16_t buttons) {
    const uint16_t pressed=buttons&~previousButtons_;
    previousButtons_=buttons;
    if(pressed&StartButton){paused_=!paused_;Sound(290);}
    if(pressed&Select) {if(completed_)Start(memory);else Enter(memory,checkpoint_);paused_=false;return;}
    if(paused_ || completed_) return;
    if(invulnerability_)--invulnerability_;
    if(escapeFrames_>0 && --escapeFrames_==0){
        // Restart the attempt, including Keyzer and switch object state.
        Start(memory);return;
    }
    if(transitionTimer_) --transitionTimer_;
    if(attackTimer_) --attackTimer_;
    const int input=((buttons&Right)!=0)-((buttons&Left)!=0);
    const bool wasGrounded=player_.grounded;
    const int feetCol=player_.x/64;
    const int midRow=std::max(0,(player_.y-72)/64);
    const uint16_t ladder=room_.AttributeAt(1,feetCol,midRow);
    if((buttons&(Up|Down)) && (ladder==121 || ladder==122)) player_.climbing=true;
    if(player_.climbing && (ladder!=121 && ladder!=122)) player_.climbing=false;
    if(player_.climbing) {
        player_.x=feetCol*64+32;
        player_.vx=0;player_.vy=0;
        const int vertical=((buttons&Down)!=0)-((buttons&Up)!=0);
        Move(0,vertical*4,-120);
        SetPose(vertical ? 40 : 41);
        if(pressed&A) {player_.climbing=false;player_.vy=-176;player_.grounded=false;}
    } else {
        if(input && !attackTimer_) player_.right=input>0;
        if((pressed&B) && player_.pose!=27){attackTimer_=46;Sound(30);}
        if(attackTimer_) {
            const int target=player_.right ? 96 : -96;
            player_.vx+=std::clamp(target-player_.vx,-4,4);
        } else if(input) {
            const int maximum=(buttons&Run) && wasGrounded ? 128 : 64;
            player_.vx+=std::clamp(input*maximum-player_.vx,-6,6);
        } else player_.vx+=std::clamp(-player_.vx,-10,10);
        const bool crouched=wasGrounded && ((buttons&Down) || !Fits(player_.x,player_.y,-126));
        if(crouched) player_.vx+=std::clamp(-player_.vx,-6,6);
        if((pressed&A) && wasGrounded) {
            player_.vy=crouched ? -128 : -176;
            player_.grounded=false;
            Sound(13);
        }
        if(!(buttons&A) && player_.vy<0) player_.vy=0;
        if((pressed&Down) && !wasGrounded) {SetPose(27);player_.vx=0;attackTimer_=0;player_.vy=128;}
        const int top=crouched ? -62 : sprites_.Hitbox(player_.pose)[1];
        const int dy=std::max(-16,divideFloor(player_.vy,8));
        Move(divideFloor(player_.vx,8),dy,top);
        if(!player_.grounded) player_.vy=std::min(128,player_.vy+(player_.pose==27 ? 16 : 8));
        else player_.vy=0;
        if(player_.pose==27 && !player_.grounded) { /* preserve ground-pound pose */ }
        else if(!player_.grounded) SetPose(attackTimer_ ? 24 : 4);
        else if(crouched) SetPose(std::abs(player_.vx)>8 ? 7 : 5);
        else if(attackTimer_) SetPose(22);
        else SetPose(std::abs(player_.vx)>2 ? ((buttons&Run) ? 23 : 0) : 2);
    }
    UpdateObjects(memory,pressed);
    if(player_.y>room_.PixelHeight()*4+128) {Enter(memory,checkpoint_);return;}
    if(!transitionTimer_) {
        const int x=player_.x/64,y=player_.y/64;
        for(size_t i=1;i<entrances_.size();++i) {
            const auto& entry=entrances_[i];
            if(entry.room!=room_.RoomId() || x<entry.left || x>entry.right || y<entry.top || y>entry.bottom) continue;
            if(entry.type==2 || (pressed&Up) || (entry.type==3 && (pressed&Down))) {
                Enter(memory,entry.destination);break;
            }
        }
    }
    FollowCamera();
    Animate();
}

void NativeGame::BuildVideo(VideoState* video) const {
    if(!video) return;
    room_.BuildVideo(cameraX_,cameraY_,video);
    sprites_.Draw(player_.pose,player_.right,player_.vy>=0,frame_,
                  player_.x/4-cameraX_,player_.y/4-cameraY_,video);
    const auto& animation=sprites_.Animation(player_.pose,player_.right,player_.vy>=0);
    objects_.Draw(cameraX_,cameraY_,animation[frame_%animation.size()].objects.size(),video);
    if(invulnerability_ && (invulnerability_&4))for(size_t i=0;i<animation[frame_%animation.size()].objects.size();++i){
        video->oam[i*8]=0;video->oam[i*8+1]=2;
    }
}

void NativeGame::RememberObject(uint8_t slot,uint8_t state){
    for(auto& item:objectStates_)if(item[0]==room_.RoomId()&&item[1]==slot){item[2]=state;return;}
    objectStates_.push_back({{room_.RoomId(),slot,state}});
}

void NativeGame::UpdateObjects(const NativeMemory& memory,uint16_t pressed){
    for(auto& object:objects_.Objects()){
        if(!object.active)continue;
        const bool enemy=object.id==17||object.id==18||object.id==19||object.id==42||object.id==43;
        if(enemy && !object.stunned){
            const int direction=object.right ? 1 : -1;
            const int next=object.x+direction*2;
            if(SolidAt(next+direction*36,object.y-32,false,object.y) || !Supported(next,object.y-1))object.right=!object.right;
            else object.x=next;
        }
        if(object.stunned)--object.stunned;
        NativeObjects::Animate(&object);
        const int up=object.id==7 ? 168 : (enemy ? 80 : 64);
        const int radius=object.id==6 ? 56 : 40;
        const bool overlaps=player_.x+28>=object.x-radius && player_.x-28<=object.x+radius &&
            player_.y>=object.y-up && player_.y-120<=object.y+16;
        if(!overlaps)continue;
        if(object.id==8 && !keyzer_){keyzer_=true;object.active=false;RememberObject(object.slot,1);Sound(309);}
        else if(object.id==6){score_+=1000;object.active=false;RememberObject(object.slot,1);Sound(314);}
        else if(object.id==13 && health_<8){++health_;object.active=false;RememberObject(object.slot,1);Sound(320);}
        else if(object.id<=5 && !object.opened && (attackTimer_ || player_.pose==27)){
            object.opened=true;object.frame=0;object.timer=0;RememberObject(object.slot,2);
            if(object.id<4)jewels_|=1u<<object.id;
            if(object.id==5)health_=8;
            score_+=500;Sound(220);
        }else if(object.id==7 && !object.opened && player_.grounded){
            object.opened=true;object.frame=0;object.timer=0;RememberObject(object.slot,2);
            gSwitchStates[4]=1;escapeFrames_=180*60;music_=681;Sound(217);
        }else if(object.id==41 && escapeFrames_>0 && keyzer_){completed_=true;escapeFrames_=0;Sound(314);}
        else if(enemy){
            if(attackTimer_ || player_.pose==27 || (player_.vy>0 && player_.y<object.y-48)){
                if(object.stunned && !(pressed&B) && player_.pose!=27)continue;
                if(object.stunned || player_.pose==27){object.active=false;RememberObject(object.slot,3);score_+=100;Sound(312);}
                else{object.stunned=180;object.frame=0;object.timer=0;Sound(351);}
                if(player_.vy>0){player_.vy=-128;player_.grounded=false;}
            }else if(!object.stunned && !invulnerability_){
                --health_;invulnerability_=120;player_.vx=player_.x<object.x ? -96 : 96;player_.vy=-128;player_.grounded=false;
                attackTimer_=0;Sound(19);
                if(health_<=0){health_=8;Enter(memory,checkpoint_);return;}
            }
        }
    }
}
} // namespace wl4
