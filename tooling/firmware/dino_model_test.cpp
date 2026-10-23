#include "apps/dino_model.hpp"
using namespace dino;
constexpr bool jumpAndLand(){Game g;g.reset();g.spawnDistance=100000;g.jump();const float velocity=g.velocity;g.jump();if(velocity!=g.velocity)return false;
    float top=g.y;for(int i=0;i<50;++i){if(g.step(0))return false;if(g.y<top)top=g.y;}
    return !g.jumping&&g.y==Ground-Height&&top<100&&g.score>0;
}
constexpr bool walkHits(){Game g;g.reset();g.spawnDistance=100000;g.obstacles[0]={70,13,25,0};for(int i=0;i<30;++i)if(g.step(0))return !g.alive;return false;}
constexpr bool canClear(float speed,int kind){Game g;g.reset();g.speed=speed;g.spawnDistance=100000;g.obstacles[0]={X+speed*13,kind==2?27:13,kind==1?35:25,kind};g.jump();for(int i=0;i<55;++i)if(g.step(0))return false;return !g.jumping;}
static_assert(jumpAndLand());static_assert(walkHits());
constexpr bool everySpeedClears(){
    for(float speed=StartSpeed;speed<=MaxSpeed+.01f;speed+=.1f)
        for(int kind=0;kind<3;++kind)if(!canClear(speed,kind))return false;
    return canClear(MaxSpeed,0)&&canClear(MaxSpeed,1)&&canClear(MaxSpeed,2);
}
static_assert(everySpeedClears(),"Every jump-only obstacle must be passable across the entire speed ramp");
constexpr bool frozenAfterDeath(){Game g;g.reset();g.alive=false;g.jump();return !g.jumping&&!g.step(0)&&g.frames==0&&g.score==0;}
static_assert(frozenAfterDeath());
constexpr bool scoreSaturates(){Game g;g.reset();g.score=99999999;g.distance=9;g.spawnDistance=100000;g.step(0);return g.score==99999999;}
static_assert(scoreSaturates());
constexpr bool pairClears(float speed,int first,int second){
    Game g;g.reset();g.speed=speed;g.spawnDistance=100000;
    const float start=X+speed*13;
    g.obstacles[0]={start,first==2?27:13,first==1?35:25,first};
    g.obstacles[1]={start+safeGap(speed),second==2?27:13,second==1?35:25,second};
    unsigned jumps=0,grounded=100;
    for(int t=0;t<150;++t){
        if(!g.jumping){
            ++grounded;
            for(const auto &o:g.obstacles)if(o.x>X+20&&o.x<=X+g.speed*13){
                if(grounded<RecoverySteps)return false;
                g.jump();++jumps;grounded=0;break;
            }
        }
        if(g.step(0))return false;
    }
    return jumps==2&&!g.jumping;
}
constexpr bool allPairs(){
    for(float speed=StartSpeed;speed<=MaxSpeed+.01f;speed+=.2f)
        for(int first=0;first<3;++first)for(int second=0;second<3;++second)
            if(!pairClears(speed,first,second))return false;
    for(int a=0;a<3;++a)for(int b=0;b<3;++b)if(!pairClears(MaxSpeed,a,b))return false;
    return true;
}
static_assert(allPairs(),"Minimum-gap pairs allow landing and at least 128ms of grounded input time");
constexpr bool generatedRun(uint32_t seed,float speed){
    Game g;g.reset();g.speed=speed;unsigned passed=0,ground=0,air=0;
    for(int t=0;t<6000;++t){
        if(!g.jumping)for(const auto &o:g.obstacles)if(o.x>X+20&&o.x<=X+g.speed*13){g.jump();++passed;break;}
        seed=seed*1664525u+1013904223u;
        if(g.step(seed)||g.speed>MaxSpeed)return false;
        for(const auto &b:g.bonuses)if(b.active){
            if(b.y==GroundBonusY)++ground;else {if(b.y<118||b.y>134)return false;++air;}
            for(const auto &o:g.obstacles){
                const float cx=o.x+o.width*.5f,cy=Ground-o.height*.5f;
                const float nx=cx<b.x?b.x:cx>b.x+BonusSize?b.x+BonusSize:cx;
                const float ny=cy<b.y?b.y:cy>b.y+BonusSize?b.y+BonusSize:cy;
                const float dx=nx-cx,dy=ny-cy;
                if(dx*dx+dy*dy<bonusRadius(g.speed)*bonusRadius(g.speed))return false;
                if(overlaps({b.x,b.y,12,12},{o.x,float(Ground-o.height),float(o.width),float(o.height)}))return false;
            }
        }
    }
    return passed>50&&g.lastBonus>0&&ground>100&&air>100;
}
static_assert(generatedRun(1,StartSpeed));static_assert(generatedRun(913,StartSpeed));
static_assert(generatedRun(54,MaxSpeed));static_assert(generatedRun(0xffffffff,MaxSpeed));
constexpr bool bonusOnlyOnce(){
    Game g;g.reset();g.spawnDistance=100000;g.bonuses[0]={X+10,Ground-Height+12,50,true};
    g.step(0);if(g.bonuses[0].active||g.score!=50||g.lastBonus!=50)return false;
    g.score=0;g.distance=0;g.step(0);return g.score==0;
}
static_assert(bonusOnlyOnce());
constexpr bool bonusSaturates(){Game g;g.reset();g.score=99999990;g.addScore(50);return g.score==99999999;}
static_assert(bonusSaturates());
constexpr bool bonusesReachable(){
    for(float speed=StartSpeed;speed<=MaxSpeed+.01f;speed+=.2f)for(int height=118;height<=134;++height){
        Game g;g.reset();g.speed=speed;g.spawnDistance=100000;
        g.bonusDistance=100000;
        g.bonuses[0]={X+speed*9,float(height),50,true};
        g.jump();for(int t=0;t<60;++t)if(g.step(0))return false;
        if(g.lastBonus!=50)return false;
        g.reset();g.speed=speed;g.spawnDistance=g.bonusDistance=100000;
        g.bonuses[0]={X+speed*9,float(height),50,true};
        for(int t=0;t<60;++t)if(g.step(0))return false;
        if(g.lastBonus)return false; // Walking past an air item is a valid choice.
        g.bonuses[0]={X+speed*9,GroundBonusY,20,true};
        for(int t=0;t<60;++t)if(g.step(0))return false;
        if(g.lastBonus!=20)return false; // Ground items need no jump.
    }
    return true;
}
static_assert(bonusesReachable(),"Every air height is reachable or skippable; ground items are collected on foot");
constexpr bool excludesFutureObstacle(){
    Game g;g.reset();g.spawnDistance=1;
    if(g.bonusSpace(SpawnX))return false;
    g.spawnDistance=1000;
    g.obstacles[0]={SpawnX-20,13,25,0};
    if(g.bonusSpace(SpawnX))return false;
    g.obstacles[0].x=-80;
    if(!g.bonusSpace(SpawnX))return false;
    g.spawnBonus(0);g.spawnBonus(0x10000);
    // A new item cannot overwrite an active one or stack another on it.
    unsigned count=0;for(const auto &b:g.bonuses)if(b.active){++count;if(b.y!=GroundBonusY)return false;}
    return count==1;
}
static_assert(excludesFutureObstacle());
static_assert(bonusRadius(MaxSpeed)*4==MaxSpeed*flightSteps(MaxSpeed));
// At the slowest speed, longest jump, widest cactus and maximum random extra
// gap, both obstacles reach the player within the acceleration look-ahead.
static_assert((safeGap(StartSpeed)+64+244)/StartSpeed<LookAheadSteps);
constexpr bool shieldLifecycle(){
    Game g;g.reset();g.spawnDistance=g.bonusDistance=100000;g.shield=true;
    g.obstacles[0]={float(X+8),13,25,0};
    if(g.step(0)||g.shield||g.invulnerable!=94||!g.obstacles[0].spentCollision)return false;
    for(unsigned i=0;i<100;++i)if(g.step(0))return false;
    if(g.invulnerable)return false;
    g.obstacles[1]={float(X+8),13,25,0};return g.step(0)&&!g.alive;
}
static_assert(shieldLifecycle());
constexpr bool shieldReachable(){
    for(float speed=StartSpeed;speed<=MaxSpeed+.01f;speed+=.2f)for(int kind=0;kind<3;++kind){
        Game g;g.reset();g.speed=speed;g.spawnDistance=g.bonusDistance=100000;
        g.obstacles[0]={X+speed*13,kind==2?27:13,kind==1?35:25,kind,true};g.jump();
        for(unsigned f=0;f<55;++f)if(g.step(0))return false;
        if(!g.shield||!g.shieldFrame||g.obstacles[0].shieldItem)return false;
    }return true;
}
static_assert(shieldReachable(),"Each shield is reachable while clearing its obstacle");
constexpr bool shieldMilestones(){
    Game g;g.reset();g.score=999;g.spawnDistance=0;g.step(0);
    if(g.shieldsSpawned)return false;
    g.score=1000;g.spawnDistance=0;g.step(0);
    if(g.shieldsSpawned!=1||!g.obstacles[1].shieldItem)return false;
    g.spawnDistance=0;g.step(0);if(g.shieldsSpawned!=1||g.obstacles[2].shieldItem)return false;
    g.score=2000;g.obstacles[0].x=-80;g.spawnDistance=0;g.step(0);
    return g.shieldsSpawned==2&&g.obstacles[0].shieldItem;
}
static_assert(shieldMilestones());
