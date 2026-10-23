"""Execute production filter, BSP rotation/touch callbacks and GUI policy on a host.

SPI, LVGL and IMU endpoints are simulated; this is not physical-panel acceptance.
"""
from pathlib import Path
import argparse,json,os,subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir',type=Path,required=True)
    args=ap.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True,exist_ok=False)
    (run/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 258\n#define ESP_ERR_INVALID_STATE 259\n',encoding='utf-8')
    board=(ROOT/'firmware/components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c').read_text(encoding='utf-8')
    board=board[board.index('static esp_err_t orientation_commands('):board.index('esp_err_t bsp_touch_new(')]
    gui=(ROOT/'firmware/main/core/screen_orientation.cpp').read_text(encoding='utf-8')
    gui=gui[gui.index('namespace sys {'):]
    harness=r'''
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "core/orientation_model.hpp"
#include "bsp/orientation.h"
static unsigned checks=0;
#define CHECK(c) do {++checks;if(!(c)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);std::exit(1);}} while(0)
#define ESP_RETURN_ON_ERROR(expr,...) do {int err=(expr);if(err)return err;} while(0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
struct Panel {bool swapped=false,mx=false,my=false;int gx=0,gy=0;} hardware;
static Panel *panel_handle=&hardware;
static unsigned panel_orientation=0;
static int commandCount=0,failCommand=0;
static bool commandFails(){return ++commandCount==failCommand;}
static int esp_lcd_panel_swap_xy(Panel *p,bool v){if(commandFails())return ESP_FAIL;p->swapped=v;return ESP_OK;}
static int esp_lcd_panel_mirror(Panel *p,bool x,bool y){if(commandFails())return ESP_FAIL;p->mx=x;p->my=y;return ESP_OK;}
static int esp_lcd_panel_set_gap(Panel *p,int x,int y){if(commandFails())return ESP_FAIL;p->gx=x;p->gy=y;return ESP_OK;}
using esp_lcd_touch_handle_t=void *;
BOARD
struct lv_obj_t {};
struct lv_display_t {lv_obj_t screen,top,system;};
struct lv_indev_t {bool pressed=false,scrolling=false;};
static lv_display_t screen;
static lv_indev_t input;
static uint32_t nowMs=0;
static bool irqPending=false;
static unsigned resets=0,invalidates=0;
static constexpr int LV_INDEV_STATE_PRESSED=1;
static int lv_indev_get_state(lv_indev_t *p){return p->pressed?1:0;}
static lv_obj_t *lv_indev_get_scroll_obj(lv_indev_t *p){return p->scrolling?&screen.screen:nullptr;}
static bool bsp_power_touch_pending(){return irqPending;}
static void bsp_power_touch_reset(){++resets;}
static void lv_indev_reset(lv_indev_t *,void *){++resets;}
static void lv_obj_invalidate(lv_obj_t *){++invalidates;}
static lv_obj_t *lv_display_get_screen_active(lv_display_t *p){return &p->screen;}
static lv_obj_t *lv_display_get_layer_top(lv_display_t *p){return &p->top;}
static lv_obj_t *lv_display_get_layer_sys(lv_display_t *p){return &p->system;}
static int64_t esp_timer_get_time(){return int64_t(nowMs)*1000;}
namespace sys {
struct Acceleration{float x=0,y=0,z=0;};
struct AccelerationSample{Acceleration value;uint32_t timeMs=0;};
struct Settings {static bool permission;static bool motionSensorOn(){return permission;}};
bool Settings::permission=false;
struct Power {static bool lit,sleep,slow;static bool isScreenOn(){return lit;}static bool sleepPending(){return sleep;}static bool slowIdle(){return slow;}};
bool Power::lit=true,Power::sleep=false,Power::slow=false;
struct Screenshot {static bool free;static bool idle(){return free;}};
bool Screenshot::free=true;
struct Qmi8658 {static bool demand,valid;static AccelerationSample sample;static void setInUse(bool v){demand=v;}static bool cachedAcceleration(AccelerationSample &v){v=sample;return valid;}};
bool Qmi8658::demand=false,Qmi8658::valid=true;
AccelerationSample Qmi8658::sample;
struct ScreenOrientation {static void init(lv_display_t *,lv_indev_t *);static void poll(bool);static void mapAcceleration(Acceleration &);};
}
GUI
static void fresh(float x,float y,float z,bool locked=false){nowMs+=50;sys::Qmi8658::sample={{x,y,z},nowMs};sys::ScreenOrientation::poll(locked);}
static void hold(float x,float y,float z,unsigned frames=60,bool locked=false){for(unsigned i=0;i<frames;++i)fresh(x,y,z,locked);}
static void boot(){hardware={};panel_orientation=0;commandCount=failCommand=0;nowMs=0;input={};irqPending=false;sys::Settings::permission=false;sys::Power::lit=true;sys::Power::sleep=sys::Power::slow=false;sys::Screenshot::free=true;sys::Qmi8658::valid=true;sys::ScreenOrientation::init(&screen,&input);}
int main(){
    // Exhaust every pixel: independent forward rotation and production inverse.
    for(unsigned r=0;r<4;++r)for(unsigned y=0;y<240;++y)for(unsigned x=0;x<240;++x){
        // Independent ST7789 GRAM addressing: MV exchanges RAM axes, MX/MY
        // reverse physical columns/rows across 240x320 controller memory.
        const auto mode=bsp_orientation_panel(r);
        const unsigned column=x+mode.x_gap,row=y+mode.y_gap;
        uint16_t px=mode.swap_xy?row:column,py=mode.swap_xy?column:row;
        if(mode.mirror_x)px=239-px;
        if(mode.mirror_y)py=319-py;
        CHECK(px<240&&py<240);
        bsp_orientation_point(r,&px,&py);CHECK(px==x&&py==y);
    }
    // Every panel window stays inside the controller's addressable GRAM.
    for(unsigned r=0;r<4;++r){auto p=bsp_orientation_panel(r);CHECK(p.x_gap>=0&&p.y_gap>=0);CHECK(p.x_gap+240<=(p.swap_xy?320:240));CHECK(p.y_gap+240<=(p.swap_xy?240:320));}
    boot();CHECK(bsp_display_get_orientation()==0);
    CHECK(bsp_display_set_orientation(4)==ESP_ERR_INVALID_ARG);
    panel_handle=nullptr;CHECK(bsp_display_set_orientation(1)==ESP_ERR_INVALID_STATE);panel_handle=&hardware;
    // Fail each step, roll back all previous commands, retain touch mapping.
    for(unsigned old=0;old<4;++old)for(unsigned next=0;next<4;++next)if(old!=next){
        failCommand=0;CHECK(bsp_display_set_orientation(old)==ESP_OK);
        const auto previous=hardware;
        for(int step=1;step<=3;++step){commandCount=0;failCommand=step;CHECK(bsp_display_set_orientation(next)==ESP_FAIL);CHECK(panel_orientation==old);CHECK(hardware.swapped==previous.swapped&&hardware.mx==previous.mx&&hardware.my==previous.my&&hardware.gx==previous.gx&&hardware.gy==previous.gy);}
    }
    failCommand=0;
    // Physical corner touch callback, malformed point filtering and capacity.
    for(unsigned r=0;r<4;++r){CHECK(bsp_display_set_orientation(r)==ESP_OK);uint16_t x[]={0,239,240,17},y[]={0,239,10,999},strength[]={1,2,3,4};uint8_t count=4;orientation_touch(nullptr,x,y,strength,&count,4);CHECK(count==2&&strength[0]==1&&strength[1]==2);CHECK(x[0]<240&&y[0]<240&&x[1]<240&&y[1]<240);}
    boot();hold(1,0,0);CHECK(panel_orientation==0); // off never rotates
    sys::Settings::permission=true;hold(1,0,0,10);CHECK(panel_orientation==0); // short tilt
    hold(1,0,0);CHECK(panel_orientation==3);
    sys::Settings::permission=false;hold(-1,0,0);CHECK(panel_orientation==3); // off freezes
    sys::Settings::permission=true;hold(-1,0,0,60,true);CHECK(panel_orientation==3&&!sys::Qmi8658::demand); // app entry lock
    hold(-1,0,0);CHECK(panel_orientation==1&&sys::Qmi8658::demand); // exit unlock
    hold(0,-1,0);CHECK(panel_orientation==2);hold(0,1,0);CHECK(panel_orientation==0);
    // All deferral causes reset the stability window, keeping current direction.
    for(unsigned reason=0;reason<7;++reason){
        input.pressed=reason==0;input.scrolling=reason==1;irqPending=reason==2;
        sys::Power::lit=reason!=3;sys::Power::sleep=reason==4;sys::Power::slow=reason==5;sys::Screenshot::free=reason!=6;
        hold(1,0,0);CHECK(panel_orientation==0);
        input={};irqPending=false;sys::Power::lit=true;sys::Power::sleep=sys::Power::slow=false;sys::Screenshot::free=true;
        hold(1,0,0,8);CHECK(panel_orientation==0);hold(0,1,0);
    }
    // Flat, diagonal, freefall, shock, nonfinite and stale readings cannot turn.
    hold(0,0,1);hold(.7f,.7f,0);hold(.1f,0,0);hold(3,0,0);hold(std::numeric_limits<float>::quiet_NaN(),0,0);hold(std::numeric_limits<float>::infinity(),0,0);CHECK(panel_orientation==0);
    hold(1,0,0,8);for(unsigned i=0;i<100;++i){nowMs+=50;sys::ScreenOrientation::poll(false);}CHECK(panel_orientation==0);
    sys::Qmi8658::valid=false;hold(1,0,0);CHECK(panel_orientation==0);sys::Qmi8658::valid=true;
    // Acceleration aligns with logical game directions for every entry pose.
    for(unsigned r=0;r<4;++r){CHECK(bsp_display_set_orientation(r)==ESP_OK);for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y){float px=x,py=y;for(unsigned i=0;i<r;++i){float old=px;px=-py;py=old;}sys::Acceleration a{px,py,.5f};sys::ScreenOrientation::mapAcceleration(a);CHECK(a.x==x&&a.y==y&&a.z==.5f);}}
    boot();CHECK(panel_orientation==0&&!sys::Settings::permission);
    // Millisecond wrap-around and failed commit do not bypass stable timing.
    sys::orientation::Filter f;f.committed(0,UINT32_MAX-2000);int result=-1;
    for(uint32_t i=0;i<=900;i+=50){uint32_t t=UINT32_MAX-500+i;result=f.update(1,0,0,t,t,true);if(i<800)CHECK(result<0);}
    CHECK(result==3&&f.current()==0);f.failed(1000);CHECK(f.current()==0);
    for(uint32_t t=1050;t<2200;t+=50)CHECK(f.update(1,0,0,t,t,true)<0);
    CHECK(f.update(1,0,0,2200,2200,true)==3);f.committed(3,2200);CHECK(f.current()==3);
    std::printf("{\"passed\":true,\"assertions\":%u,\"hardware_simulated\":true}\n",checks);
}
'''.replace('BOARD',board).replace('GUI',gui)
    source=run/'replay.cpp';source.write_text(harness,encoding='utf-8')
    compiler=Path(__import__('shutil').which('g++') or 'g++').resolve()
    env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    exe=run/'replay.exe'
    command=[str(compiler),'-std=c++17','-O2','-Wall','-Wextra','-static','-I'+str(run),
             '-I'+str(ROOT/'firmware/main'),'-I'+str(ROOT/'firmware/components/esp32_s3_touch_lcd_1_54/include'),str(source),'-o',str(exe)]
    p=subprocess.run(command,env=env,capture_output=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'compile.log').write_bytes(p.stdout+p.stderr);p.check_returncode()
    p=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'runtime.log').write_bytes(p.stdout+p.stderr);p.check_returncode()
    result=json.loads(p.stdout);(run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()
