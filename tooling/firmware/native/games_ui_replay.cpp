// Real LVGL/rendering and production game logic; hardware adapters only are fake.
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/game_widgets.h"
#include "core/event_bus.hpp"
#include "core/button_mgr.hpp"
#include "core/qmi8658.hpp"
#define private public
#include "apps/dino.hpp"
#include "apps/gravity_maze.hpp"
#undef private
#include "apps/dino_sprites.hpp"
#include "apps/maze_visual.hpp"
static unsigned checks=0,allocations=0,sequence=0,pageKeys=0,lease=0;
static bool failFog=false,failFloor=false;
static std::map<uint32_t,sys::EventHandler> handlers;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static void *heap_caps_malloc(size_t n,int){if((failFog&&n==maze::Fog::Size)||(failFloor&&n==240*240*2))return nullptr;++allocations;return std::malloc(n);}
static void heap_caps_free(void *p){if(p){--allocations;std::free(p);}}
static int64_t esp_timer_get_time(){return int64_t(lv_tick_get())*1000;}
static uint32_t esp_random(){return ++sequence*1664525u;}
namespace sys {
struct Power{static bool isScreenOn(){return true;}static bool sleepPending(){return false;}};
struct Settings{static uint32_t dinoHighScore(){return 10000;}static void recordDinoScore(uint32_t){}};
uint32_t EventBus::subscribe(EventType,EventHandler h){handlers[++sequence]=h;return sequence;}
void EventBus::unsubscribe(uint32_t n){handlers.erase(n);}
uint32_t Buttons::claimPageKeys(){return pageKeys=++sequence;}
void Buttons::releasePageKeys(uint32_t n){if(n==pageKeys)pageKeys=0;}
uint32_t Qmi8658::claimTemporaryUse(){return lease=++sequence;}
void Qmi8658::releaseTemporaryUse(uint32_t n){if(n==lease)lease=0;}
bool Qmi8658::temporaryAcceleration(uint32_t n,AccelerationSample &s){s={{0,0,1},lv_tick_get()};return n&&n==lease;}
}
namespace ui {
struct WindowManager {static WindowManager &instance(){static WindowManager m;return m;}void goHome(){}};
void TextInput::hide(){}void TextInput::show(lv_obj_t *){}bool TextInput::visible(){return false;}void TextInput::bindField(lv_obj_t *){}
}
namespace core {void WorkerWake::notify(){}namespace game_audio {std::shared_ptr<Session> start(Track t){return std::make_shared<Session>(t);}}}
#include "compact_ui.inc"
#include "dino.inc"
#include "gravity_maze.inc"
static uint8_t framebuffer[240*240*2];
static void advance(){lv_tick_inc(16);lv_timer_handler();lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);}
static void capture(const char *name){
    advance();FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<sizeof(framebuffer);i+=2){unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;
        unsigned char rgb[]={uint8_t((c>>11)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};std::fwrite(rgb,1,3,f);}
    std::fclose(f);
}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static unsigned objects(lv_obj_t *o){unsigned n=1;for(unsigned i=0;i<lv_obj_get_child_count(o);++i)n+=objects(lv_obj_get_child(o,i));return n;}
static void terrainEffects(){
    unsigned longPatches=0;
    for(unsigned level=0;level<maze::Levels;++level){
        const auto b=maze::generate(level);auto bare=b;bare.trapCount=0;
        CHECK(b.trapCount==maze::targetTraps(level));
        unsigned types[4]{},quarters[4]{};
        float shortest=240,longest=0;
        for(unsigned k=0;b.trapCount>k;++k){
            const auto t=b.traps[k];const auto r=t.area;
            ++types[unsigned(t.kind)];++quarters[maze::quadrant(r.x+r.w/2,r.y+r.h/2)];
            const float length=(t.side==maze::Up||t.side==maze::Down)?r.w:r.h;
            shortest=std::min(shortest,length);longest=std::max(longest,length);
            if(length>b.cell())++longPatches;
            bool effective=false;
            // Find a physically reachable contact point and exercise the real
            // ball update, rather than just checking a terrain enum or colour.
            for(int y=int(r.y);!effective&&y<int(r.y+r.h)+7;++y)
                for(int x=int(r.x)-7;!effective&&x<int(r.x+r.w)+7;++x){
                    if(maze::blocked(bare,x,y)||maze::touchesGoal(b,x,y))continue;
                    if(t.kind==maze::Hazard::Bumper){
                        effective=maze::touchesHazard(t,x,y,maze::BallRadius)&&maze::blocked(b,x,y);continue;
                    }
                    if(!maze::touchesHazard(t,x,y,t.kind==maze::Hazard::Pit?maze::BallRadius*.4f:0))continue;
                    maze::Ball actual;actual.x=x;actual.y=y;actual.vx=.3f;
                    auto control=actual;actual.step(b,0,0);control.step(bare,0,0);
                    if(t.kind==maze::Hazard::Pit)effective=actual.falling&&!control.falling;
                    else if(!actual.falling&&actual.impact==0&&control.impact==0)
                        effective=t.kind==maze::Hazard::Sand?actual.vx<control.vx:actual.vx>control.vx;
                }
            CHECK(effective);
            for(unsigned j=0;j<k;++j)if(t.kind==b.traps[j].kind){
                const auto other=b.traps[j].area;
                const float dx=r.x+r.w/2-other.x-other.w/2,dy=r.y+r.h/2-other.y-other.h/2;
                CHECK(dx*dx+dy*dy>=6400);CHECK(t.corridor!=b.traps[j].corridor);
            }
        }
        for(unsigned i=0;i<4;++i){CHECK(types[i]>=2&&types[i]<=3);CHECK(quarters[i]>=b.trapCount/4&&quarters[i]<=(b.trapCount+3)/4);}
        CHECK(longest-shortest>3);
    }
    CHECK(longPatches>=maze::Stages);
}
static void terrainRoutes(){
    for(unsigned level=0;level<15;++level){
        const auto b=maze::generate(level);
        std::vector<uint8_t> safe(240*240),seen(240*240);std::vector<int> queue;
        for(int y=0;y<240;++y)for(int x=0;x<240;++x){
            bool ok=!maze::blocked(b,x,y,maze::BallRadius+.25f);
            for(unsigned i=0;ok&&i<b.trapCount;++i)if(b.traps[i].kind==maze::Hazard::Pit)
                ok=!maze::touchesHazard(b.traps[i],x,y,maze::BallRadius+.25f);
            safe[y*240+x]=ok;
        }
        int start=int(b.centerY(0))*240+int(b.centerX(0));CHECK(safe[start]);queue.push_back(start);seen[start]=1;
        for(size_t head=0;head<queue.size();++head){const int at=queue[head];
            for(int d:{-240,240,-1,1}){const int to=at+d;
                if(to<0||to>=240*240||((d==-1||d==1)&&to/240!=at/240)||seen[to]||!safe[to])continue;
                seen[to]=1;queue.push_back(to);
            }
        }
        CHECK(seen[int(b.centerY(b.goal))*240+int(b.centerX(b.goal))]);
        // Every cell remains reachable, even when its centre is under a patch.
        for(unsigned at=0;at<b.n*b.n;++at){bool reached=false;
            const int x=b.origin()+int(at%b.n)*b.cell(),y=b.origin()+int(at/b.n)*b.cell();
            for(int yy=y+3;yy<y+b.cell()-3;++yy)for(int xx=x+3;xx<x+b.cell()-3;++xx)reached|=seen[yy*240+xx]!=0;
            CHECK(reached);
        }
    }
}
static void fogModel(){
    uint8_t pixels[maze::Fog::Size];maze::Fog f;
    for(unsigned level=0;level<15;++level){auto b=maze::generate(level);f.reset(pixels,b,level);
        CHECK(f.radius()==b.roadWidth()*3/4);f.update(40,40);
        if(!maze::Fog::mode(level)){for(auto p:pixels)CHECK(p==0);continue;}
        CHECK(pixels[40*240+40]==0&&pixels[220*240+220]==255);
        bool gradient=false;for(int x=41;x<40+f.radius();++x)if(pixels[40*240+x]>0&&pixels[40*240+x]<255)gradient=true;
        CHECK(gradient);CHECK(!f.update(40,40));f.update(190,190);
        CHECK(pixels[190*240+190]==0);CHECK(pixels[40*240+40]==(level%3==1?0:255));
        CHECK(maze::Fog::opacity(false)==255&&maze::Fog::opacity(true)==64);
    }
}
int main(){
    terrainEffects();terrainRoutes();fogModel();lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
    ui::theme::init(10);const auto baseTimers=timers();
    for(unsigned cycle=0;cycle<3;++cycle){
        auto root=ui::compact::panel(lv_screen_active(),240,240);apps::GravityMaze a;a.setRoot(root);a.onCreate(root);a.onShow();
        CHECK(allocations==2);advance();
        lv_area_t h{},l{};lv_obj_get_coords(a.hint,&h);lv_obj_get_coords(a.levels,&l);CHECK(h.y2<l.y1);
        if(!cycle)capture("maze-menu.ppm");
        for(unsigned level=0;level<15;++level){
            a.level=level;a.buildBoard();a.play();advance();CHECK(a.active&&lease&&a.audio);
            CHECK(bool(a.fogView)==bool(level%3));
            if(a.fogView){
                CHECK(a.fogPixels[0]==255);CHECK(framebuffer[0]==0&&framebuffer[1]==0);
                CHECK(lv_obj_get_style_image_opa(a.fogView,LV_PART_MAIN)==255);
            }
            const unsigned count=objects(a.screen);for(int step=0;step<3;++step){a.ball.x+=1;a.paintBall();advance();}
            CHECK(objects(a.screen)==count&&allocations==2);
            if(!cycle&&(level<3||level%3==0)){char name[32];std::snprintf(name,sizeof(name),"maze-mode%u.ppm",level);capture(name);}
            a.pause();CHECK(!lease&&!a.active);
        }
        lv_obj_send_event(a.badge,LV_EVENT_CLICKED,nullptr);a.tick();CHECK(a.developer);
        CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(a.badge,LV_PART_MAIN))==lv_color_to_u32(lv_color_white()));
        CHECK(lv_obj_get_style_image_opa(a.fogView,LV_PART_MAIN)==64);a.play();advance();
        CHECK(framebuffer[0]!=0||framebuffer[1]!=0);if(!cycle)capture("maze-developer.ppm");
        auto weak=std::weak_ptr<core::game_audio::Session>(a.audio);a.onDestroy();lv_obj_delete(root);advance();
        CHECK(!allocations&&!lease&&!pageKeys&&handlers.empty()&&weak.expired()&&timers()==baseTimers);
    }
    failFog=true;{
        auto root=ui::compact::panel(lv_screen_active(),240,240);apps::GravityMaze a;a.onCreate(root);a.onShow();
        a.level=1;a.buildBoard();a.play();CHECK(!a.active&&!lease);a.onDestroy();lv_obj_delete(root);advance();CHECK(!allocations);
    }failFog=false;
    failFloor=true;{
        auto root=ui::compact::panel(lv_screen_active(),240,240);apps::GravityMaze a;a.onCreate(root);a.onShow();
        CHECK(!a.floorPixels);a.play();advance();CHECK(a.active);capture("maze-fallback.ppm");
        a.onDestroy();lv_obj_delete(root);advance();CHECK(!allocations&&!lease&&!pageKeys);
    }failFloor=false;
    for(unsigned cycle=0;cycle<3;++cycle){
        auto root=ui::compact::panel(lv_screen_active(),240,240);apps::Dino a;a.setRoot(root);a.onCreate(root);a.onShow();a.start();
        a.game.shield=true;a.paint();advance();CHECK(!lv_obj_is_hidden(a.shieldGlow));
        if(!cycle)capture("dino-shield.ppm");const unsigned count=objects(a.screen);
        a.game.shield=false;a.game.invulnerable=94;a.game.frames=5;a.paint();CHECK(lv_obj_is_hidden(a.rex));
        a.game.frames=10;a.paint();CHECK(!lv_obj_is_hidden(a.rex));
        CHECK(lv_obj_is_hidden(a.shieldGlow)&&objects(a.screen)==count);
        auto weak=std::weak_ptr<core::game_audio::Session>(a.audio);a.onDestroy();lv_obj_delete(root);advance();
        CHECK(!allocations&&!lease&&!pageKeys&&handlers.empty()&&weak.expired()&&timers()==baseTimers);
    }
    std::printf("%u checks: real LVGL games, 15 levels, black/gradient/trail fog, developer toggle, allocation failure, shields and exit resource release.\n",checks);
}
