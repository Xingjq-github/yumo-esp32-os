#include "core/usb_storage_io.hpp"
#include "core/recorder_transport.hpp"
#include "core/game_synth.hpp"
#include "apps/maze_visual.hpp"
#define API extern "C" __declspec(dllexport)
#define REQUIRE(x) do {if(!(x))return __LINE__;}while(0)
API int usb_probe() {
    uint8_t disk[4096],sector[512],data[1200],out[1200];int error=0,reads=0,writes=0;
    for(unsigned i=0;i<4096;++i)disk[i]=uint8_t(i*17+9);
    auto read=[&](uint64_t lba,uint8_t *p){++reads;if(lba>=8)return 7;std::memcpy(p,disk+lba*512,512);return 0;};
    auto write=[&](uint64_t lba,const uint8_t *p){++writes;if(lba>=8)return 7;std::memcpy(disk+lba*512,p,512);return 0;};
    auto io=[&](uint32_t lba,uint32_t off,void *p,uint32_t n,bool wr){return core::usb_disk::transfer(8,lba,off,p,n,wr,sector,error,read,write);};
    REQUIRE(io(0,497,out,1100,false)==1100&&reads==4&&writes==0);
    for(unsigned i=0;i<1100;++i)REQUIRE(out[i]==disk[497+i]);
    for(unsigned i=0;i<1200;++i)data[i]=uint8_t(i*7+3);
    reads=writes=0;REQUIRE(io(1,0,data,1024,true)==1024&&reads==0&&writes==2);
    reads=writes=0;const uint8_t before=disk[496],after=disk[497+1100];
    REQUIRE(io(0,497,data,1100,true)==1100&&reads==2&&writes==4);
    REQUIRE(disk[496]==before&&disk[1597]==after);
    for(unsigned i=0;i<1100;++i)REQUIRE(disk[497+i]==data[i]);
    reads=writes=0;
    REQUIRE(io(7,512,nullptr,0,true)==0&&reads==0&&writes==0);
    REQUIRE(io(7,511,out,2,false)<0&&error==0&&reads==0&&writes==0);
    REQUIRE(io(UINT32_MAX,UINT32_MAX,out,1,false)<0&&reads==0);
    REQUIRE(io(0,0,nullptr,1,true)<0&&writes==0);
    REQUIRE(io(0,0,out,UINT32_MAX,false)<0&&reads==0);
    auto failRead=[&](uint64_t,uint8_t *){return 71;};
    REQUIRE(core::usb_disk::transfer(8,0,1,data,10,true,sector,error,failRead,write)<0&&error==71&&writes==0);
    auto failWrite=[&](uint64_t lba,const uint8_t *p){if(++writes==2)return 72;std::memcpy(disk+lba*512,p,512);return 0;};
    REQUIRE(core::usb_disk::transfer(8,0,0,data,1200,true,sector,error,read,failWrite)<0&&error==72&&writes==2);
    return 0;
}
API unsigned speed_probe(const int16_t *in,unsigned count,unsigned chunk,int16_t *out) {
    if(chunk==0||chunk>1024)return 0;
    core::recorder::RewindSpeed speed;int16_t buffer[1024];unsigned total=0;
    for(unsigned at=0;at<count;) {
        const auto n=std::min(chunk,count-at);
        for(unsigned i=0;i<n;++i)buffer[i]=in[at+i];
        const auto m=speed.process(buffer,n);
        for(unsigned i=0;i<m;++i)out[total++]=buffer[i];
        at+=n;
    }
    return total;
}
API int synth_probe() {
    using namespace core::game_audio;
    const unsigned effects[]={Jump,Bonus,Lost,Impact,Fall,Goal};
    for(unsigned effect:effects) {
        Synth s;s.trigger(effect);unsigned nonzero=0;
        for(unsigned i=0;i<Synth::effectLength(effect);++i)if(s.sample(false))++nonzero;
        REQUIRE(!s.effect&&nonzero>100&&s.sample(false)==0);
    }
    Synth s;unsigned nonzero=0;
    for(unsigned i=0;i<Synth::Rate*8;++i){const int value=s.sample(true);REQUIRE(value>-8192&&value<8192);if(value)++nonzero;}
    REQUIRE(nonzero>Synth::Rate*6);
    s.trigger(4);(void)s.sample(true);s.trigger(1);REQUIRE(s.effect==1&&s.effectSample==0);
    REQUIRE(Synth::priority(Jump|Impact|Fall|Goal|Silence)==Goal);
    REQUIRE(Synth::priority(Silence)==0);
    Synth runner,maze(Track::Maze);unsigned different=0;
    for(unsigned i=0;i<Synth::Rate*10;++i)if(runner.sample(true)!=maze.sample(true))++different;
    REQUIRE(different>Synth::Rate*9);
    uint32_t signatures[5]{};
    for(unsigned material=0;material<5;++material) {
        Synth moving(Track::Maze),stationary(Track::Maze);unsigned rolling=0;
        for(unsigned i=0;i<Synth::Rate;++i) {
            const auto value=moving.sample(true,Synth::movement(material,100));
            if(value!=stationary.sample(true))++rolling;
            signatures[material]=signatures[material]*33+uint16_t(value);
            REQUIRE(value>-8192&&value<8192);
        }
        REQUIRE(rolling>Synth::Rate*9/10);
        for(unsigned previous=0;previous<material;++previous)REQUIRE(signatures[material]!=signatures[previous]);
        for(unsigned effect:effects) {
            moving.trigger(effect);
            for(unsigned i=0;i<Synth::effectLength(effect);++i) {
                const auto value=moving.sample(true,Synth::movement(material,100));
                REQUIRE(value>-8192&&value<8192);
            }
            REQUIRE(!moving.effect);
        }
        moving.trigger(Fall);moving.silence();
        for(unsigned i=0;i<256;++i)REQUIRE(moving.sample(false)==0);
        REQUIRE(!moving.effect&&!moving.rollGain);
    }
    return 0;
}
API int maze_probe() {
    for(unsigned level=0;level<maze::Levels;++level) {
        auto b=maze::generate(level);REQUIRE(b.n==5+level/3&&b.trapCount==2+level/3);
        REQUIRE(b.route[0]&&b.route[b.goal]);
        for(unsigned i=0;i<b.trapCount;++i) {
            const auto t=b.traps[i];REQUIRE(t.cell&&t.cell!=b.goal);
            for(unsigned j=0;j<i;++j)REQUIRE(t.cell!=b.traps[j].cell);
            if(t.kind==maze::Hazard::Pit) {
                REQUIRE(!b.route[t.cell]);maze::Ball p;p.x=b.centerX(t.cell);p.y=b.centerY(t.cell);
                REQUIRE(!p.step(b,0,0)&&p.falling==24);
                for(unsigned n=0;n<24;++n)REQUIRE(!p.step(b,0,0));
                REQUIRE(!p.falling&&p.x==b.centerX(0)&&p.y==b.centerY(0));
            }
        }
        // Long random paths across every generated wall/corner/material.
        maze::Ball ball;ball.reset(b);uint32_t seed=100+level;
        for(unsigned frame=0;frame<8000;++frame) {
            const float ax=float(int(maze::random(seed)%201)-100)/100;
            const float ay=float(int(maze::random(seed)%201)-100)/100;
            if(ball.step(b,ax,ay))ball.reset(b);
            REQUIRE(!maze::blocked(b,ball.x,ball.y));
        }
    }
    // Each material can leave sand; friction must never freeze the solution.
    for(unsigned m=0;m<5;++m) {
        maze::Board b;b.material=maze::Material(m);b.goal=24;b.trapCount=1;b.traps[0]={6,maze::Hazard::Sand};
        maze::Ball ball;ball.x=b.centerX(6);ball.y=b.centerY(6);
        for(unsigned n=0;n<100;++n)ball.step(b,1,0);
        REQUIRE(ball.x>b.centerX(6)+b.cell()*.27f);
    }
    REQUIRE(maze::physics(maze::Material::Rubber).rebound>maze::physics(maze::Material::Steel).rebound);
    REQUIRE(maze::physics(maze::Material::Glass).drag>maze::physics(maze::Material::Wood).drag);
    maze::Board wall;wall.goal=24;maze::Ball hit;hit.reset(wall);
    hit.x=wall.origin()+3+maze::BallRadius;hit.vx=-1;
    REQUIRE(!maze::blocked(wall,hit.x,hit.y));
    REQUIRE(!hit.step(wall,0,0)&&hit.impact>0&&hit.vx>=0&&!maze::blocked(wall,hit.x,hit.y));
    hit.reset(wall);REQUIRE(!hit.impact&&!hit.step(wall,0,0)&&!hit.impact);
    return 0;
}
API void render_maze(unsigned level,uint16_t *floor,uint32_t *sphere,int *details) {
    const auto b=maze::generate(level);maze::Ball ball;ball.reset(b);
    maze::visual::background(floor,b,level);maze::visual::sphere(sphere,b,ball);
    details[0]=int(ball.x);details[1]=int(ball.y);details[2]=int(b.centerX(b.goal));details[3]=int(b.centerY(b.goal));
}
