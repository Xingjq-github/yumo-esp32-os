#include "lvgl.h"
#include "ui/screen_protection.hpp"
#include "ui/screen_protection_model.hpp"
#include "core/power_mgr.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static unsigned checks=0,flushes=0,presses=0,moves=0,releases=0,clicks=0,cycles=0;
static uint32_t tick=0,generation=1;
static bool lit=true,sleeping=false,pressed=false,wasPressed=false;
static int px=80,py=80,hour=12,minute=34;
static lv_indev_t *pointer=nullptr;
static uint8_t pixels[240*240*2],home[sizeof(pixels)],covered[sizeof(pixels)],bufferA[240*20*2],bufferB[sizeof(bufferA)];
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
bool sys::Power::isScreenOn(){return lit;}
bool sys::Power::sleepPending(){return sleeping;}
uint32_t sys::Power::activityGeneration(){return generation;}
time_t replay_time(time_t *out){if(out)*out=1700000000;return 1700000000;}
tm *replay_localtime(const time_t *,tm *out){*out={};out->tm_hour=hour;out->tm_min=minute;return out;}
static uint32_t getTick(){return tick;}
static void flush(lv_display_t *d,const lv_area_t *r,uint8_t *p){
    ++flushes;
    CHECK(r->x1>=0 && r->y1>=0 && r->x2<240 && r->y2<240);
    for(int y=r->y1;y<=r->y2;++y)std::memcpy(pixels+(y*240+r->x1)*2,p+(y-r->y1)*(r->x2-r->x1+1)*2,(r->x2-r->x1+1)*2);
    lv_display_flush_ready(d);
}
static void rawRead(lv_indev_t *,lv_indev_data_t *data){
    if(pressed || wasPressed)++generation; // Same activity contract as the board adapter.
    wasPressed=pressed;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    data->point={px,py};data->continue_reading=false;
}
static void render(){lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);}
static void poll(bool allowed=true){ui::ScreenProtection::poll(allowed);render();}
static void advance(uint32_t delta,bool allowed=true){tick+=delta;poll(allowed);}
static lv_obj_t *cover(){CHECK(ui::ScreenProtection::visible());return lv_obj_get_child(lv_layer_top(),-1);}
static lv_obj_t *clockObject(){return lv_obj_get_child(cover(),0);}
static void touch(bool state,int x=80,int y=80){pressed=state;px=x;py=y;tick+=20;lv_indev_read(pointer);poll();}
static void counter(lv_event_t *e){switch(lv_event_get_code(e)){
    case LV_EVENT_PRESSED:++presses;break;case LV_EVENT_PRESSING:++moves;break;
    case LV_EVENT_RELEASED:++releases;break;case LV_EVENT_CLICKED:++clicks;break;default:break;}}
static void resetCounters(){presses=moves=releases=clicks=0;}
static void checkpoint(const char *stage){auto file=std::fopen("progress.log","a");if(file){std::fprintf(file,"%s tick=%u checks=%u flushes=%u\n",stage,tick,checks,flushes);std::fclose(file);}}
static void startCover(){ui::ScreenProtection::reset();poll();advance(119999);CHECK(!ui::ScreenProtection::visible());advance(1);CHECK(ui::ScreenProtection::visible());}
static void image(const char *name){FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");for(unsigned i=0;i<240*240;++i){const unsigned v=pixels[2*i]|(pixels[2*i+1]<<8);const unsigned char rgb[]={uint8_t(((v>>11)*255)/31),uint8_t((((v>>5)&63)*255)/63),uint8_t(((v&31)*255)/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}

int main(){
    lv_init();lv_tick_set_cb(getTick);
    auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,bufferA,bufferB,sizeof(bufferA),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    auto root=lv_screen_active();lv_obj_remove_style_all(root);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_style_bg_color(root,lv_color_hex(0xEF1838),0);lv_obj_set_scrollable(root,false);
    auto target=lv_obj_create(root);lv_obj_remove_style_all(target);lv_obj_set_size(target,240,240);lv_obj_set_clickable(target,true);lv_obj_set_scrollable(target,false);lv_obj_add_event_cb(target,counter,LV_EVENT_ALL,nullptr);
    auto icon=lv_label_create(target);lv_label_set_text(icon,"WIFI  BT  HOME");lv_obj_set_style_text_color(icon,lv_color_hex(0x00FF00),0);lv_obj_set_pos(icon,4,4);
    render();std::memcpy(home,pixels,sizeof(home));image("home.ppm");
    // Without an initialized pointer, the UI must never cover the display.
    poll();advance(120000);CHECK(!ui::ScreenProtection::visible());
    pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,rawRead);lv_indev_set_display(pointer,display);
    ui::ScreenProtection::init(pointer);ui::ScreenProtection::init(pointer); // idempotent wrapper installation
    startCover();std::memcpy(covered,pixels,sizeof(covered));image("covered.ppm");
    checkpoint("covered");
    // High-contrast home cannot contribute even one colored pixel through the cover.
    auto label=clockObject();lv_area_t area;lv_obj_get_coords(label,&area);CHECK(area.x1==16 && area.y1==22 && area.x2<240 && area.y2<240);
    const uint16_t neutral=uint16_t(pixels[0]|(pixels[1]<<8));unsigned ink=0;
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<240;++x){
        const unsigned offset=(y*240+x)*2;const uint16_t value=uint16_t(pixels[offset]|(pixels[offset+1]<<8));
        if(x<unsigned(area.x1)||x>unsigned(area.x2)||y<unsigned(area.y1)||y>unsigned(area.y2))CHECK(value==neutral);
        if(value!=neutral)++ink;
    }
    CHECK(ink>20 && std::memcmp(home,covered,sizeof(home))!=0);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x00EE44),0);lv_label_set_text(icon,"CHANGED UNDER COVER");render();CHECK(std::memcmp(covered,pixels,sizeof(pixels))==0);
    checkpoint("opaque");
    // Repeated polls with unchanged time/position produce no display transfers.
    const auto idleFlushes=flushes;for(int i=0;i<700;++i)advance(20);CHECK(flushes==idleFlushes);
    checkpoint("idle");
    unsigned positions=0;const uint32_t entry=tick-14000;
    for(unsigned n=0;n<8;++n){tick=entry+n*15000;poll();auto p=ui::protection::position(n*15000);lv_obj_get_coords(clockObject(),&area);CHECK(area.x1==p.x && area.y1==p.y && area.x2<240 && area.y2<240);++positions;}
    const auto beforeClock=flushes;minute=35;advance(1000);CHECK(flushes>beforeClock && std::strcmp(lv_label_get_text(clockObject()),"12:35")==0);
    checkpoint("positions");
    ++generation;poll();CHECK(!ui::ScreenProtection::visible());
    // The first wake touch is consumed across object deletion, dragging and release.
    startCover();resetCounters();touch(true);CHECK(!ui::ScreenProtection::visible());
    for(int i=0;i<15;++i)touch(true,50+i*4,80+i*3);touch(false,120,130);
    CHECK(presses==0 && moves==0 && releases==0 && clicks==0);
    touch(true);touch(false);CHECK(presses==1 && releases==1 && clicks==1);
    checkpoint("touch");
    // BSP can prime the raw packet once before the LVGL input timer reads it again.
    startCover();resetCounters();pressed=true;lv_indev_data_t packet{};lv_indev_get_read_cb(pointer)(pointer,&packet);CHECK(packet.state==LV_INDEV_STATE_RELEASED);poll();
    CHECK(!ui::ScreenProtection::visible());touch(true);touch(false);CHECK(presses==0 && clicks==0);touch(true);touch(false);CHECK(clicks==1);
    checkpoint("prime-read");
    // Input activity at the exact threshold wins over screen protection.
    ui::ScreenProtection::reset();poll();tick+=120000;++generation;poll();CHECK(!ui::ScreenProtection::visible());
    advance(119999);CHECK(!ui::ScreenProtection::visible());advance(1);CHECK(ui::ScreenProtection::visible());
    for(int reason=0;reason<3;++reason){
        startCover();if(reason==0)lit=false;if(reason==1)sleeping=true;poll(reason!=2);CHECK(!ui::ScreenProtection::visible());
        lit=true;sleeping=false;poll();advance(119999);CHECK(!ui::ScreenProtection::visible());advance(1);CHECK(ui::ScreenProtection::visible());
    }
    // Native LVGL run crosses the 49-day tick wrap as well as constexpr model tests.
    ui::ScreenProtection::reset();tick=0xffff0000u;generation=0xffffffffu;poll();advance(119999);CHECK(!ui::ScreenProtection::visible());advance(1);CHECK(ui::ScreenProtection::visible());
    generation=0;poll();CHECK(!ui::ScreenProtection::visible());
    checkpoint("boundaries");
    // Repeated navigation fences release both LVGL objects and preserve an ongoing wake stroke.
    ui::ScreenProtection::reset();render();const auto baselineChildren=lv_obj_get_child_count(lv_layer_top());lv_mem_monitor_t initial{};lv_mem_monitor(&initial);
    for(unsigned i=0;i<100;++i){
        startCover();CHECK(lv_obj_get_child_count(lv_layer_top())==baselineChildren+1);resetCounters();touch(true);
        ui::ScreenProtection::reset();touch(true,90,90);touch(false,90,90);CHECK(presses==0 && clicks==0);
        poll(false);CHECK(lv_obj_get_child_count(lv_layer_top())==baselineChildren);CHECK(lv_mem_test()==LV_RESULT_OK);++cycles;
    }
    lv_mem_monitor_t final{};lv_mem_monitor(&final);CHECK(final.free_size==initial.free_size);
    checkpoint("cycles");
    ui::ScreenProtection::reset();lv_indev_delete(pointer);checkpoint("input-deleted");
    lv_display_delete(display);checkpoint("display-deleted");
    // The device keeps LVGL running for its lifetime. Leave its host draw workers
    // to process exit: the cached Windows port force-terminates threads in deinit.
    std::printf("{\"checks\":%u,\"positions\":%u,\"unchanged_poll_flushes\":0,\"cycles\":%u,\"leaked_bytes\":%lld,\"draw_workers\":%d,\"first_touch_leaks\":0}\n",checks,positions,cycles,(long long)initial.free_size-(long long)final.free_size,LV_DRAW_SW_DRAW_UNIT_CNT);
}
