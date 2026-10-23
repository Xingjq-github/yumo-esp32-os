// Native small-screen adaptation of Chromium's T-Rex runner jump/collision model.
// Chromium 124.0.6367.91 components/neterror/resources/offline.js (BSD-3-Clause).
// See main/third_party/chromium-LICENSE. Touch-only gameplay omits duck-only birds.
#pragma once
#include <cstdint>
namespace dino {
constexpr int Ground=190, X=24, Width=31, Height=33;
constexpr unsigned StepMs=16;
constexpr float StartSpeed=3.2f, MaxSpeed=5.8f, Acceleration=.001f, Gravity=.43f;
constexpr unsigned RecoverySteps=8, LookAheadSteps=160;
constexpr float jumpVelocity(float speed){return -7.1f-speed*.07f;}
constexpr unsigned flightSteps(float speed){
    float height=0,velocity=jumpVelocity(speed);unsigned steps=0;
    do {height+=velocity;velocity+=Gravity;++steps;} while(height<0&&steps<100);
    return steps;
}
constexpr float futureSpeed(float speed){
    const float future=speed+Acceleration*LookAheadSteps;
    return future<MaxSpeed?future:MaxSpeed;
}
// Half of one jump's travel is the exclusion DIAMETER. Bound the
// speed increase while the item travels from the spawn edge to the player.
constexpr float bonusRadius(float speed){return speed*flightSteps(speed)*.25f;}
constexpr float BonusSize=12,SpawnX=244,GroundBonusY=Ground-BonusSize-2;
constexpr float safeGap(float speed){
    // Bound acceleration until both this obstacle and the next reach the player.
    // A complete longest jump + a grounded input window + the widest cactus,
    // with two simulation steps for spawn/landing quantization. No double walls.
    const float bound=futureSpeed(speed);
    return bound*(flightSteps(MaxSpeed)+RecoverySteps+2)+27;
}
struct Rect {float x,y,w,h;};
constexpr bool overlaps(Rect a,Rect b){return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;}
struct Obstacle {float x=260;int width=13,height=28,kind=0;bool shieldItem=false,spentCollision=false;};
struct Bonus {float x=-80,y=120;unsigned points=20;bool active=false;};
struct Game {
    float y=Ground-Height,velocity=0,speed=StartSpeed,distance=0,spawnDistance=160,bonusDistance=36;
    Obstacle obstacles[3]{};
    Bonus bonuses[3]{};
    bool alive=false,jumping=false;
    unsigned frames=0,bonusFrame=0,lastBonus=0;uint32_t score=0;
    bool shield=false;
    unsigned invulnerable=0,shieldFrame=0,shieldHitFrame=0;
    uint32_t shieldsSpawned=0;
    constexpr void reset(){*this=Game{};for(auto &o:obstacles)o.x=-80;alive=true;}
    constexpr void jump(){if(alive&&!jumping){velocity=jumpVelocity(speed);jumping=true;}}
    constexpr void addScore(uint32_t points){score=score>99999999-points?99999999:score+points;}
    constexpr Rect body() const {return {float(X+3),y+4,float(Width-7),float(Height-6)};}
    constexpr bool hit(const Obstacle &o) const {
        // Multiple smaller body boxes, as in Chromium, forgive transparent pixels.
        const Rect head{float(X+13),y+2,16,12},torso{float(X+6),y+12,15,15},feet{float(X+7),y+25,15,6};
        Rect cactus{o.x+2,float(Ground-o.height+3),float(o.width-4),float(o.height-3)};
        return overlaps(head,cactus)||overlaps(torso,cactus)||overlaps(feet,cactus);
    }
    constexpr bool bonusClear(float x,float y,const Obstacle &o) const {
        const float cx=o.x+o.width*.5f,cy=Ground-o.height*.5f;
        const float nearX=cx<x?x:cx>x+BonusSize?x+BonusSize:cx;
        const float nearY=cy<y?y:cy>y+BonusSize?y+BonusSize:cy;
        const float dx=nearX-cx,dy=nearY-cy,r=bonusRadius(futureSpeed(speed));
        return dx*dx+dy*dy>=r*r;
    }
    constexpr bool bonusSpace(float x,float y=GroundBonusY) const {
        for(const auto &o:obstacles){
            if(!bonusClear(x,y,o))return false;
        }
        // Also reserve the NEXT cactus, which is not in the object pool yet.
        // Its left edge can be one simulation step earlier than the countdown.
        const float next=SpawnX+spawnDistance-MaxSpeed;
        for(int kind=0;kind<3;++kind)
            if(!bonusClear(x,y,{next,kind==2?27:13,kind==1?35:25,kind}))return false;
        for(const auto &b:bonuses)if(b.active){
            const float delta=x-b.x;
            if(delta>-48&&delta<48)return false;
        }
        return true;
    }
    constexpr void spawnBonus(uint32_t entropy){
        const bool air=(entropy&0x10000)!=0;
        const float height=air?118.0f+float((entropy>>20)%17):GroundBonusY;
        if(!bonusSpace(SpawnX,height))return;
        for(auto &b:bonuses)if(!b.active){
            b={SpawnX,height,air?50u:20u,true};
            bonusDistance=65+float((entropy>>9)%61);return;
        }
    }
    constexpr Rect shieldBox(const Obstacle &o) const {return {o.x+o.width*.5f-8,float(Ground-o.height-44),16,18};}
    constexpr bool step(uint32_t entropy) {
        if(!alive)return false;
        ++frames;speed=speed+Acceleration<MaxSpeed?speed+Acceleration:MaxSpeed;
        if(invulnerable)--invulnerable;
        if(jumping){y+=velocity;velocity+=Gravity;if(y>=Ground-Height){y=Ground-Height;velocity=0;jumping=false;}}
        for(auto &o:obstacles){
            o.x-=speed;
            if(o.shieldItem && overlaps(body(),shieldBox(o))){o.shieldItem=false;shield=true;shieldFrame=frames;}
            if(o.x>-o.width && !o.spentCollision && !invulnerable && hit(o)){
                if(shield){shield=false;shieldHitFrame=frames;invulnerable=94;o.spentCollision=true;}
                else {alive=false;return true;}
            }
        }
        for(auto &b:bonuses)if(b.active){
            b.x-=speed;
            if(b.x<-12)b.active=false;
            else if(overlaps(body(),{b.x,b.y,12,12})){
                addScore(b.points);lastBonus=b.points;bonusFrame=frames;b.active=false;
            }
        }
        spawnDistance-=speed;
        if(spawnDistance<=0){
            for(unsigned i=0;i<3;++i)if(obstacles[i].x<-40){
                auto &o=obstacles[i];
                o.kind=int(entropy%3);o.height=o.kind==1?35:25;o.width=o.kind==2?27:13;o.x=SpawnX;
                o.spentCollision=false;o.shieldItem=score/1000>shieldsSpawned;
                if(o.shieldItem)++shieldsSpawned;
                spawnDistance=safeGap(speed)+float((entropy>>8)%64);break;
            }
        }
        bonusDistance-=speed;
        if(bonusDistance<=0){bonusDistance=12;spawnBonus(entropy);}
        distance+=speed;if(distance>=10){const auto points=uint32_t(distance/10);distance-=points*10;addScore(points);}
        return false;
    }
};
} // namespace dino
