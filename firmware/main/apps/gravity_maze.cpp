#include "gravity_maze.hpp"
#include "maze_visual.hpp"
#include "esp_heap_caps.h"
#include "compact_ui.hpp"
#include "theme.hpp"
#include "game_widgets.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "window_mgr.hpp"
#include "core/qmi8658.hpp"
#include "core/screen_orientation.hpp"
#include "core/settings_store.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "esp_timer.h"
#include <cmath>
#include <cstdio>
#include <string>
namespace apps {
namespace c=ui::compact;
namespace {
uint32_t nowMs(){return uint32_t(esp_timer_get_time()/1000);}
lv_color_t mazePaper(){return lv_color_hex(0x252A31);}
lv_color_t mazeInk(){return lv_color_hex(0xEBE6DC);}
lv_color_t mazeFloor(){return lv_color_hex(0x4D5358);}
lv_color_t mazeWall(unsigned level){
    return lv_color_hex(maze::visual::wallColors[maze::stage(level)]);
}
const char *materialName(maze::Material m){
    static const char *names[]={"钢球 · 惯性较大","石球 · 平稳滚动","木球 · 摩擦较大","胶球 · 碰撞回弹","玻璃球 · 光滑易滑"};
    return names[unsigned(m)];
}
const char *visionName(unsigned level){
    static const char *names[]={"全图可见","保留走过区域","仅当前视野"};
    return names[maze::Fog::mode(level)];
}
}
void GravityMaze::onCreate(lv_obj_t *parent) {
    floorPixels=static_cast<uint16_t *>(heap_caps_malloc(240*240*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    fogPixels=static_cast<uint8_t *>(heap_caps_malloc(maze::Fog::Size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    fogImage.header.magic=LV_IMAGE_HEADER_MAGIC;fogImage.header.cf=LV_COLOR_FORMAT_A8;
    fogImage.header.w=fogImage.header.h=240;fogImage.header.stride=240;
    fogImage.data_size=maze::Fog::Size;fogImage.data=fogPixels;
    goalImage.header.magic=LV_IMAGE_HEADER_MAGIC;goalImage.header.cf=LV_COLOR_FORMAT_ARGB8888;
    goalImage.header.w=goalImage.header.h=18;goalImage.header.stride=72;
    goalImage.data_size=sizeof(goalPixels);goalImage.data=reinterpret_cast<const uint8_t *>(goalPixels);
    for(int y=0;y<18;++y)for(int x=0;x<18;++x){
        const int dx=2*x-17,dy=2*y-17;
        const int r2=dx*dx+dy*dy;
        goalPixels[y*18+x]=r2>324?0:0xff000000|(r2>196?0xA3222B:r2>100?0xDF373C:r2>36?0x101012:0xDF373C);
    }
    floorImage.header.magic=LV_IMAGE_HEADER_MAGIC;floorImage.header.cf=LV_COLOR_FORMAT_RGB565;
    floorImage.header.w=floorImage.header.h=240;floorImage.header.stride=480;
    floorImage.data_size=240*240*2;floorImage.data=reinterpret_cast<const uint8_t *>(floorPixels);
    ballImage.header.magic=LV_IMAGE_HEADER_MAGIC;ballImage.header.cf=LV_COLOR_FORMAT_ARGB8888;
    ballImage.header.w=ballImage.header.h=12;ballImage.header.stride=48;
    ballImage.data_size=sizeof(ballPixels);ballImage.data=reinterpret_cast<const uint8_t *>(ballPixels);
    screen=c::panel(parent,240,240);
    boardView=c::panel(screen,240,240);
    lv_obj_add_event_cb(boardView,[](lv_event_t *e){
        auto self=static_cast<GravityMaze *>(lv_event_get_user_data(e));if(self->floorPixels)return;
        lv_area_t origin;lv_obj_get_coords(self->boardView,&origin);
        lv_draw_rect_dsc_t paint;lv_draw_rect_dsc_init(&paint);
        for(unsigned i=0;i<self->board.trapCount;++i){
            const auto t=self->board.traps[i];const auto r=t.area;
            for(unsigned pass=0;pass<(maze::hardTrap(t.kind)?2u:1u);++pass){
            paint.bg_color=lv_color_hex(maze::hardTrap(t.kind)&&!pass?
                maze::visual::rimColor(maze::visual::floorColors[maze::stage(self->level)]):maze::visual::trapColor(t.kind));
            for(int y=int(r.y);y<int(r.y+r.h);++y){
                int left=-1;
                for(int x=int(r.x);x<=int(r.x+r.w);++x){
                    const bool inside=x<int(r.x+r.w)&&(pass?maze::touchesHazard(t,x+.5f,y+.5f):maze::touchesTrap(t,x+.5f,y+.5f));
                    if(inside&&left<0)left=x;
                    if(!inside&&left>=0){lv_area_t line={origin.x1+left,origin.y1+y,origin.x1+x-1,origin.y1+y};lv_draw_rect(lv_event_get_layer(e),&paint,&line);left=-1;}
                }
            }
            }
        }
    },LV_EVENT_DRAW_MAIN,this);
    lv_obj_add_event_cb(boardView,[](lv_event_t *e){auto s=static_cast<GravityMaze *>(lv_event_get_user_data(e));if(s->active)s->pending=Action::Menu;},LV_EVENT_CLICKED,this);
    createMenu();
    screenSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &){pause("已暂停，点击继续");});
    timer=lv_timer_create([](lv_timer_t *t){static_cast<GravityMaze *>(lv_timer_get_user_data(t))->tick();},50,this);
    buildBoard();
}
void GravityMaze::createMenu(){
    // The paused maze stays underneath this instrument-panel card. Rebuilding a level
    // only cleans boardView, so the overlay and its callbacks remain valid.
    menu=game_overlay(screen);
    lv_obj_set_style_bg_color(menu,mazeInk(),0);lv_obj_set_style_bg_opa(menu,LV_OPA_30,0);
    menuCard=game_box(menu,8,4,224,232,mazePaper(),6);
    lv_obj_set_style_border_width(menuCard,2,0);lv_obj_set_style_border_color(menuCard,mazeInk(),0);
    badge=game_box(menu,176,16,40,40,mazeFloor(),4);
    lv_obj_set_clickable(badge,true);
    lv_obj_add_event_cb(badge,[](lv_event_t *e){static_cast<GravityMaze *>(lv_event_get_user_data(e))->pending=Action::Developer;},LV_EVENT_CLICKED,this);
    badgeBall=lv_image_create(badge);lv_image_set_src(badgeBall,&ballImage);
    lv_obj_set_clickable(badgeBall,false);
    lv_image_set_scale(badgeBall,640);lv_obj_center(badgeBall);
    heading=game_text(menu,"重力迷宫",20,17,144,mazeInk());
    notice=game_text(menu,"",20,41,148,mazeInk());
    for(unsigned i=0;i<maze::Stages;++i)stages[i]=game_box(menu,20+int(i)*40,67,32,5,mazeFloor(),2);
    hint=game_text(menu,"倾斜控制 · 触屏暂停\n碰到终点即可过关",20,73,200,mazeInk());
    lv_obj_set_style_text_font(hint,ui::theme::fontCjkFull(),0);
    lv_obj_set_style_text_line_space(hint,-2,0);
    std::string options;for(unsigned i=1;i<=maze::Levels;++i){if(i>1)options+='\n';options+="第 "+std::to_string(i)+" 关   "+std::to_string(maze::dimension(i-1))+" x "+std::to_string(maze::dimension(i-1));}
    levels=c::dropdown(menu,options.c_str(),200);lv_obj_set_pos(levels,20,116);lv_obj_set_height(levels,36);
    lv_obj_add_event_cb(levels,[](lv_event_t *e){static_cast<GravityMaze *>(lv_event_get_user_data(e))->pending=Action::Select;},LV_EVENT_VALUE_CHANGED,this);
    auto button=[&](const char *text,Action a,int x,int y){auto b=game_button(menu,text,x,y,96,36,mazePaper(),mazeInk(),mazeWall(level),4,actionCb,this);lv_obj_set_user_data(b,reinterpret_cast<void *>(uintptr_t(a)));return b;};
    menuActions[0]=resume=button("开始",Action::Play,20,158);menuActions[1]=button("重来",Action::Restart,124,158);
    menuActions[2]=button("切换关卡",Action::Next,20,198);menuActions[3]=button("退出",Action::Exit,124,198);
}
void GravityMaze::actionCb(lv_event_t *e){auto s=static_cast<GravityMaze *>(lv_event_get_user_data(e));s->pending=static_cast<Action>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));}
void GravityMaze::buildBoard(){
    if(audio)audio->pause();
    releaseSensor();active=false;won=false;hasPlayed=false;board=maze::generate(level);ball.reset(board);
    lv_obj_clean(boardView);
    fogView=nullptr;fog.reset(fogPixels,board,level);
    const auto wallColor=mazeWall(level);
    const auto floorColor=lv_color_hex(maze::visual::floorColors[maze::stage(level)]);
    lv_obj_set_style_bg_color(boardView,floorColor,0);
    if(floorPixels) {
        lv_image_cache_drop(&floorImage);
        maze::visual::background(floorPixels,board,level);
        auto floor=lv_image_create(boardView);lv_image_set_src(floor,&floorImage);
        lv_obj_set_clickable(floor,false);
    } else {
      maze::eachWall(board,[&](maze::Rect r){
        auto wall=game_box(boardView,int(r.x),int(r.y),int(r.w),int(r.h),wallColor,1);
        // One-pixel light/shade within the collider gives a raised toy rail.
        const bool horizontal=r.w>r.h;
        game_box(wall,0,0,horizontal?int(r.w):1,horizontal?1:int(r.h),lv_color_mix(mazePaper(),wallColor,140),0);
        game_box(wall,horizontal?0:2,horizontal?2:0,horizontal?int(r.w):1,horizontal?1:int(r.h),lv_color_mix(mazeInk(),wallColor,100),0);
      });
    }
    const int gx=int(board.centerX(board.goal)),gy=int(board.centerY(board.goal));
    // The visible 18px target and the contact radius come from the same model.
    const int radius=int(maze::GoalRadius);
    auto goal=lv_image_create(boardView);lv_image_set_src(goal,&goalImage);
    lv_obj_set_pos(goal,gx-radius,gy-radius);lv_obj_set_clickable(goal,false);
    auto start=game_box(boardView,int(board.centerX(0))-7,int(board.centerY(0))-7,14,14,floorColor,7);
    lv_obj_set_style_border_width(start,1,0);lv_obj_set_style_border_color(start,wallColor,0);
    shadow=game_box(boardView,0,0,12,7,lv_color_black(),4);lv_obj_set_style_bg_opa(shadow,LV_OPA_40,0);
    ballView=lv_image_create(boardView);lv_image_set_src(ballView,&ballImage);lv_obj_set_clickable(ballView,false);
    if(fogPixels&&maze::Fog::mode(level)){
        lv_image_cache_drop(&fogImage);fogView=lv_image_create(boardView);lv_image_set_src(fogView,&fogImage);
        lv_obj_set_style_image_recolor(fogView,lv_color_black(),0);
        lv_obj_set_style_image_recolor_opa(fogView,LV_OPA_COVER,0);
        lv_obj_set_style_image_opa(fogView,maze::Fog::opacity(developer),0);
        lv_obj_set_clickable(fogView,false);
    }
    paintBall();
    char text[48];std::snprintf(text,sizeof(text),"%u / %u  ·  %u x %u",level+1,maze::Levels,board.n,board.n);c::setText(notice,text);
    for(unsigned i=0;i<maze::Stages;++i)lv_obj_set_style_bg_color(stages[i],i<=maze::stage(level)?mazeWall(i*3):mazeFloor(),0);
    for(unsigned i=0;i<4;++i)game_button_colors(menuActions[i],i==0?wallColor:mazePaper(),i==0?mazePaper():mazeInk(),wallColor);
    game_dropdown_colors(levels,mazePaper(),mazeInk(),wallColor,wallColor);
    lv_obj_set_style_border_color(menuCard,wallColor,0);
    c::setText(lv_obj_get_child(resume,0),"开始");
    lv_dropdown_set_selected(levels,level);
    pause();
}
void GravityMaze::paintBall(){
    maze::visual::sphere(ballPixels,board,ball);lv_image_cache_drop(&ballImage);
    lv_obj_invalidate(ballView);lv_obj_invalidate(badgeBall);
    lv_obj_set_pos(ballView,int(ball.x)-6,int(ball.y)-6);
    lv_obj_set_pos(shadow,int(ball.x)-4,int(ball.y)+2);
    if(fogView&&fog.update(int(ball.x),int(ball.y))){lv_image_cache_drop(&fogImage);lv_obj_invalidate(fogView);}
}
void GravityMaze::releaseSensor(){sys::Qmi8658::releaseTemporaryUse(lease);lease=0;sampled=false;ax=ay=0;ball.stop();}
void GravityMaze::play(){
    if(active||!timer||!visible||!sys::Power::isScreenOn()||sys::Power::sleepPending())return;
    if(!sys::Settings::motionSensorOn()){pause("请先开启重力感应");return;}
    if(won){level=(level+1)%maze::Levels;buildBoard();}
    if(maze::Fog::mode(level)&&!fogPixels){pause("内存不足\n请退出后重新进入");return;}
    lease=sys::Qmi8658::claimTemporaryUse();started=nowMs();lastStep=lv_tick_get();sampled=false;
    lastImpact=started-160;
    if(!audio)audio=core::game_audio::start(core::game_audio::Track::Maze);
    if(audio){audio->motion.store(core::game_audio::Synth::movement(unsigned(board.material),0));audio->setMusic(true);}
    active=true;hasPlayed=true;lv_obj_set_hidden(menu,true);lv_timer_set_period(timer,16);
}
void GravityMaze::pause(const char *message){
    if(audio)audio->pause();
    active=false;releaseSensor();
    if(menu)lv_obj_set_hidden(menu,false);
    if(timer)lv_timer_set_period(timer,50);
    c::setText(lv_obj_get_child(resume,0),won?(level+1==maze::Levels?"重新闯关":"下一关"):(hasPlayed?"继续":"开始"));
    const std::string details=std::string(materialName(board.material))+"\n"+visionName(level);
    c::setText(hint,message?message:details.c_str());
    lv_obj_set_style_text_color(hint,won?lv_color_hex(0x9BD3AD):mazeInk(),0);
}
bool GravityMaze::onBack(){if(active){pending=Action::Menu;return true;}return false;}
void GravityMaze::tick(){
    if(!visible)return;
    const auto action=pending;pending=Action::None;
    switch(action){
    case Action::Play:play();break;
    case Action::Restart:buildBoard();play();break;
    case Action::Select:level=lv_dropdown_get_selected(levels);buildBoard();pause();break;
    case Action::Next:level=(level+1)%maze::Levels;buildBoard();pause();break;
    case Action::Menu:pause();break;
    case Action::Developer:
        developer=!developer;lv_obj_set_style_bg_color(badge,developer?lv_color_white():mazeFloor(),0);
        if(fogView)lv_obj_set_style_image_opa(fogView,maze::Fog::opacity(developer),0);
        break;
    case Action::Exit:ui::WindowManager::instance().goHome();return;
    default:break;
    }
    if(!active)return;
    if(!sys::Power::isScreenOn()||sys::Power::sleepPending()){pause();return;}
    sys::AccelerationSample sample;const bool ok=sys::Qmi8658::temporaryAcceleration(lease,sample);const uint32_t now=nowMs();
    if(ok)sys::ScreenOrientation::mapAcceleration(sample.value);
    if(ok&&uint32_t(now-sample.timeMs)<250&&std::isfinite(sample.value.x)&&std::isfinite(sample.value.y)&&std::isfinite(sample.value.z)){
        const float norm=sample.value.x*sample.value.x+sample.value.y*sample.value.y+sample.value.z*sample.value.z;
        if(norm>.25f&&norm<3.24f){
            if(!sampled||lastSample!=sample.timeMs){ax=ax*.7f+sample.value.x*.3f;ay=ay*.7f+sample.value.y*.3f;lastSample=sample.timeMs;sampled=true;}
        }
    }
    if(!sampled||uint32_t(now-lastSample)>=250){ball.stop();if(audio)audio->motion.store(0);lastStep=lv_tick_get();if(uint32_t(now-started)>2000)pause(sys::Settings::motionSensorOn()?"重力感应不可用\n平放后点击继续重试":"请先开启重力感应");return;}
    lv_display_trigger_activity(nullptr);
    const uint32_t tick=lv_tick_get();unsigned steps=(tick-lastStep)/16;
    if(steps>3){steps=3;lastStep=tick;}else lastStep+=steps*16;
    while(steps--){
        const bool wasFalling=ball.falling!=0;
        if(ball.step(board,ax,ay)){
            won=true;pause(level+1==maze::Levels?"已完成全部15关！\n可重新选择关卡":"到达终点！\n点击下一关继续");
            if(audio)audio->trigger(core::game_audio::Goal);
            break;
        }
        if(audio){
            if(!wasFalling&&ball.falling)audio->trigger(core::game_audio::Fall);
            else if(ball.impact>.15f&&uint32_t(now-lastImpact)>=160){audio->trigger(core::game_audio::Impact);lastImpact=now;}
        }
    }
    if(audio)audio->motion.store(core::game_audio::Synth::movement(unsigned(board.material),
        active&&!ball.falling?unsigned(std::min(100.f,std::sqrt(ball.vx*ball.vx+ball.vy*ball.vy)*35.f)):0));
    paintBall();
}
void GravityMaze::onShow(){visible=true;keySession=sys::Buttons::claimPageKeys();if(timer)lv_timer_resume(timer);}
void GravityMaze::onHide(){visible=false;active=false;pending=Action::None;if(audio){audio->requestStop();audio.reset();}releaseSensor();sys::Buttons::releasePageKeys(keySession);keySession=0;if(timer)lv_timer_pause(timer);}
void GravityMaze::onDestroy(){
    onHide();sys::EventBus::unsubscribe(screenSub);screenSub=0;if(timer)lv_timer_delete(timer);timer=nullptr;
    lv_obj_clean(screen);lv_image_cache_drop(&floorImage);lv_image_cache_drop(&ballImage);
    lv_image_cache_drop(&fogImage);lv_image_cache_drop(&goalImage);
    heap_caps_free(fogPixels);fogPixels=nullptr;fogView=nullptr;
    heap_caps_free(floorPixels);floorPixels=nullptr;
}
} // namespace apps
