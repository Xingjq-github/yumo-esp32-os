// Production HomeScreen, FocusPager and LVGL input/rendering. Navigation/storage
// adapters are controlled doubles; actual NVS cache behavior is tested separately.
#include "lvgl.h"
#include "ui/home_screen.hpp"
#include "ui/compact_ui.hpp"
#include "ui/control_states.h"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include "ui/focus_pager.hpp"
#include "ui/app_icons.hpp"
#include "core/settings_store.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static tm *localtime_r(const time_t *value,tm *out){*out=*std::localtime(value);return out;}
namespace fake {
bool screen=true,home=true;
sys::app_order::Order saved;
unsigned saves=0,opens=0;size_t opened=0;
sys::app_order::SaveState state=sys::app_order::SaveState::Saved;
}
namespace sys {
struct Power {static bool isScreenOn(){return fake::screen;}static bool sleepPending(){return false;}};
app_order::Order Settings::appOrder(){return fake::saved;}
bool Settings::setAppOrder(const app_order::Order &order){fake::saved=order;++fake::saves;fake::state=app_order::SaveState::Pending;return true;}
app_order::SaveState Settings::appOrderSaveState(){return fake::state;}
}
namespace ui {
struct Entry{const char *name,*icon;uint32_t id;};
class WindowManager {
public:
    std::vector<Entry> entries;
    static WindowManager &instance(){static WindowManager w;return w;}
    const std::vector<Entry> &apps()const{return entries;}
    bool atHome()const{return fake::home;}
    void openApp(size_t index){CHECK(index<entries.size());++fake::opens;fake::opened=index;}
};
void TextInput::bindField(lv_obj_t *){}void TextInput::show(lv_obj_t *){}void TextInput::hide(){}
}
#include "compact_ui.inc"
#include "home_screen.inc"
static lv_indev_t *touch;
static lv_point_t point{};static bool down=false;
static uint8_t framebuffer[240*240*2];
static void frame(unsigned ms=20){
    lv_tick_inc(ms);lv_indev_read(touch);lv_timer_handler();ui::HomeScreen::poll();
    lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);
}
static void sample(bool pressed,int x,int y,unsigned ms=20){down=pressed;point={x,y};frame(ms);}
static void release(){sample(false,point.x,point.y);}
static void tap(int x,int y){sample(true,x,y);release();}
static lv_point_t middle(lv_obj_t *obj){lv_obj_update_layout(obj);lv_area_t a;lv_obj_get_coords(obj,&a);return {(a.x1+a.x2)/2,(a.y1+a.y2)/2};}
static void hold(lv_obj_t *obj){auto p=middle(obj);sample(true,p.x,p.y);for(int i=0;i<30;++i)frame();}
static void flush(){fake::state=sys::app_order::SaveState::Saved;frame();frame(1600);}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static void capture(const char *name){
    FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<sizeof(framebuffer);i+=2){unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;
        uint8_t rgb[]={uint8_t((c>>11)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};std::fwrite(rgb,1,3,f);}
    std::fclose(f);
}
static void revealList(int theme){
    if(theme==6)tap(120,221);
    if(theme==8){
        sample(true,120,85);for(int i=0;i<30;++i)frame();release();
        CHECK(ui::arrangePage&&ui::HomeScreen::arranging());
    }
    CHECK(ui::menuList&&ui::menuRows.size()==20);
}
int main(){
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_mode(touch,LV_INDEV_MODE_EVENT);
    lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *data){data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;data->point=point;});
    const char *names[]={"时钟","手电筒","密码本","蓝牙键鼠","录音机","蓝牙音乐","8Bit音乐","随机","FC游戏","文本","编程速查","设计模式","相册","文件管理","局域网","万年历","小恐龙","重力迷宫","摩斯电码","设置"};
    const char *keys[]={"clock","flashlight","vault","hid","recorder","bt_music","chiptune","random","fc","text","programming","patterns","photos","files","lan","calendar","dino","maze","morse","settings"};
    const char *icons[]={LV_SYMBOL_BELL,LV_SYMBOL_CHARGE,LV_SYMBOL_EYE_CLOSE,LV_SYMBOL_KEYBOARD,LV_SYMBOL_AUDIO,LV_SYMBOL_BLUETOOTH,LV_SYMBOL_PLAY,LV_SYMBOL_SHUFFLE,ui::icons::Fc,LV_SYMBOL_FILE,ui::icons::Programming,ui::icons::Patterns,LV_SYMBOL_IMAGE,LV_SYMBOL_DIRECTORY,LV_SYMBOL_WIFI,LV_SYMBOL_LIST,ui::icons::Dino,ui::icons::Maze,LV_SYMBOL_BULLET LV_SYMBOL_MINUS,LV_SYMBOL_SETTINGS};
    auto &entries=ui::WindowManager::instance().entries;
    for(unsigned i=0;i<20;++i)entries.push_back({names[i],icons[i],sys::app_order::key(keys[i])});
    frame();const auto baseTimers=timers();
    for(unsigned cycle=0;cycle<3;++cycle)for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);auto root=ui::compact::panel(lv_screen_active(),240,216);lv_obj_set_pos(root,0,24);
        ui::HomeScreen::build(root);frame();
        if(fake::saved.count)CHECK(ui::menuOrder==fake::saved);
        const auto opens=fake::opens;revealList(theme);CHECK(fake::opens==opens);
        lv_obj_scroll_to_y(ui::menuList,0,LV_ANIM_OFF);frame();
        const auto initial=ui::menuOrder;const auto moved=ui::menuRows[0].app;auto row=ui::menuRows[0].object;
        const auto saves=fake::saves;hold(row);CHECK(ui::dragging&&ui::ghost);CHECK(fake::saves==saves);
        auto target=middle(ui::menuRows[2].object);sample(true,target.x,target.y,40);
        CHECK(ui::menuOrder.ids[2]==initial.ids[0]&&fake::saves==saves);
        if(!cycle){char file[40];std::snprintf(file,sizeof(file),"home-%d-drag.ppm",theme);capture(file);}
        release();CHECK(!ui::dragging&&!ui::ghost&&fake::opens==opens&&fake::saves==saves+1);
        CHECK(fake::saved==ui::menuOrder);CHECK(ui::menuOrder.ids[19]==sys::app_order::Settings);
        fake::state=sys::app_order::SaveState::Failed;frame();CHECK(ui::noticeLabel&&std::strstr(lv_label_get_text(ui::noticeLabel),"失败"));
        flush();CHECK(!ui::noticeBox);
        auto p=middle(ui::menuRows[2].object);tap(p.x,p.y);
        if(theme==8)CHECK(fake::opens==opens);else CHECK(fake::opens==opens+1&&fake::opened==moved);
        const auto noOp=fake::saves;hold(ui::menuRows[2].object);CHECK(ui::dragging);release();CHECK(fake::saves==noOp);
        // Scroll a long distance while holding near the edge. The captured row
        // stays selected even when it moves off its original viewport position.
        lv_obj_scroll_to_y(ui::menuList,0,LV_ANIM_OFF);frame();hold(ui::menuRows[0].object);CHECK(ui::dragging);
        lv_area_t area;lv_obj_get_coords(ui::menuList,&area);
        for(int i=0;i<120;++i)sample(true,60,area.y2-2,30);
        CHECK(lv_obj_get_scroll_y(ui::menuList)>100);CHECK(ui::positionOf(ui::draggedApp)>=10);
        release();CHECK(ui::menuOrder.ids[19]==sys::app_order::Settings);flush();
        // Pinned last row is selectable normally, but cannot start a move.
        lv_obj_scroll_to_view(ui::menuRows.back().object,LV_ANIM_OFF);frame();hold(ui::menuRows.back().object);
        CHECK(!ui::dragging);const auto pinnedOpens=fake::opens;release();CHECK(fake::opens==pinnedOpens);frame(1600);
        // Cancellation (screen-off or pointer loss) restores the complete order.
        lv_obj_scroll_to_y(ui::menuList,0,LV_ANIM_OFF);frame();
        const auto before=ui::menuOrder;const auto beforeSaves=fake::saves;
        hold(ui::menuRows[0].object);target=middle(ui::menuRows[2].object);sample(true,target.x,target.y,40);
        fake::screen=false;ui::HomeScreen::poll();CHECK(!ui::dragging&&ui::menuOrder==before&&fake::saves==beforeSaves);
        release();fake::screen=true;frame();
        if(theme==8)revealList(theme);
        lv_obj_scroll_to_y(ui::menuList,0,LV_ANIM_OFF);frame();hold(ui::menuRows[0].object);
        target=middle(ui::menuRows[2].object);sample(true,target.x,target.y,40);
        lv_obj_send_event(ui::pressRow,LV_EVENT_PRESS_LOST,touch);release();CHECK(ui::menuOrder==before&&fake::saves==beforeSaves);
        // A pairing/modal layer appearing above Home cancels the move as well.
        lv_obj_scroll_to_y(ui::menuList,0,LV_ANIM_OFF);frame();hold(ui::menuRows[0].object);
        target=middle(ui::menuRows[2].object);sample(true,target.x,target.y,40);
        auto modal=ui::compact::panel(lv_layer_top(),240,240);lv_obj_update_layout(modal);frame();
        CHECK(!ui::dragging&&ui::menuOrder==before&&fake::saves==beforeSaves);release();lv_obj_delete(modal);
        // A regular swipe, even if held afterward, must never turn into sorting.
        lv_obj_scroll_to_y(ui::menuList,0,LV_ANIM_OFF);frame();p=middle(ui::menuRows[2].object);
        sample(true,p.x,p.y);sample(true,p.x,p.y-35);for(int i=0;i<30;++i)frame();CHECK(!ui::dragging);release();
        CHECK(fake::saves==beforeSaves);
        const auto selectedId=ui::focusSelectedId;
        ui::HomeScreen::cancelArrange();
        if(theme==8)CHECK(ui::menuOrder.ids[ui::focusApps.selected()]==selectedId);
        lv_indev_reset(touch,nullptr);lv_obj_delete(root);frame(200);
        CHECK(!ui::homeParent&&ui::menuRows.empty()&&ui::terminalRows.empty()&&!ui::ghost&&timers()==baseTimers);
    }
    std::printf("%u checks: five themes, actual pointer hold/drag/release, insert/auto-scroll/pin, cancellation, save feedback, navigation mapping and lifecycle\n",checks);
}
