// Production panel and LVGL input replay. Hardware service results are injected.
#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include "src/widgets/slider/lv_slider_private.h"
#include "ui/quick_panel.hpp"
#include "ui/quick_panel_model.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/control_states.h"
#include "core/system_service.hpp"
#include "core/connectivity.hpp"
#include "core/button_mgr.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace fake {
bool allowed=true,screen=true,sleep=false,panel=false,storage=true,usb=false,usbBusy=false,usbIdle=true,hotspot=false;
bool motion=false,busy[3]{};
bool mp3Busy=false,scanBusy=false;
int bright=35,vol=50,requests=0,usbCalls=0;
esp_err_t error=ESP_OK;
sys::WifiSnapshot wifi;
sys::BluetoothSnapshot bt;
sys::ServiceResult results[3];
sys::ServiceAction requested=sys::ServiceAction::Save;
bool requestedValue=false;
lv_obj_t *pairing=nullptr;
}
namespace sys {
class Settings { public:
    static int brightness(){return fake::bright;}
    static void setBrightness(int n){fake::bright=n;}
    static int volume(){return fake::vol;}
    static bool motionSensorOn(){return fake::motion;}
};
class Power { public:
    static bool isScreenOn(){return fake::screen;}
    static bool sleepPending(){return fake::sleep;}
    static void activity(){}
    static void applyBrightness(int){}
    static void screenOff(){fake::screen=false;}
};
class UsbStorage { public:
    static bool enabled(){return fake::usb;}
    static bool transitioning(){return fake::usbBusy;}
    static bool idle(){return fake::usbIdle;}
    static esp_err_t setEnabled(bool b){++fake::usbCalls;fake::usb=b;fake::usbBusy=true;fake::usbIdle=false;return ESP_OK;}
    static const char *statusText(){return "USB模式已切换";}
};
void Buttons::setSystemPanel(bool b){fake::panel=b;}
WifiSnapshot Connectivity::snapshot(){return fake::wifi;}
bool Connectivity::hotspotReserved(){return fake::hotspot;}
BluetoothSnapshot SystemService::bluetooth(){return fake::bt;}
bool SystemService::busy(ServiceDomain d){return fake::busy[unsigned(d)];}
ServiceResult SystemService::result(ServiceDomain d){return fake::results[unsigned(d)];}
const char *SystemService::errorText(esp_err_t){return "操作失败，请重试";}
esp_err_t SystemService::request(ServiceAction a,bool b){++fake::requests;fake::requested=a;fake::requestedValue=b;return fake::error;}
void SystemService::setVolume(int n){fake::vol=n;}
}
namespace core {
bool storageIdle(){return fake::storage;}
namespace sd {bool busy(){return !fake::storage;}}
namespace recorder {bool idle(){return fake::storage;}}
namespace fc {bool idle(){return fake::storage;}}
namespace game_audio {bool idle(){return fake::storage;}}
namespace mp3 {bool idle(){return !fake::mp3Busy;}bool storageIdle(){return !fake::scanBusy;}}
}
namespace vault {bool idle(){return fake::storage;}}
namespace ui {
class TextInput {public:static void hide(){}};
class ExternalKeyboard {public:static void rescope(){}};
class BluetoothPairing {public:static lv_obj_t *root(){return fake::pairing;}};
class WindowManager {public:
    static WindowManager &instance(){static WindowManager w;return w;}
    bool allowsQuickPanel(){return fake::allowed;}
};
}
#include "compact.inc"
#include "quick_panel.inc"

static unsigned checks=0,clicks=0,gestures=0,lost=0;
#define CHECK(c) do{++checks;if(!(c)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
static lv_indev_t *touch=nullptr;
static bool down=false;
static int px=0,py=0;
static uint8_t pixels[240*240*2]{},buffer[240*240*2]{};
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    for(int y=area->y1;y<=area->y2;++y)std::memcpy(pixels+(y*240+area->x1)*2,data+(y-area->y1)*(area->x2-area->x1+1)*2,(area->x2-area->x1+1)*2);
    lv_display_flush_ready(display);
}
static void capture(const char *name) {
    lv_refr_now(nullptr);FILE *file=std::fopen(name,"wb");std::fprintf(file,"P6\n240 240\n255\n");
    for(int i=0;i<240*240;++i){unsigned p=pixels[2*i]+256*pixels[2*i+1];uint8_t rgb[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};std::fwrite(rgb,1,3,file);}
    std::fclose(file);
}
static void sample(bool pressed,int x,int y) {
    down=pressed;px=x;py=y;lv_tick_inc(20);lv_indev_read(touch);ui::QuickPanel::poll();
    lv_obj_update_layout(lv_layer_top());
}
static void advance(unsigned ms) {for(unsigned i=0;i<ms;i+=20)sample(false,px,py);}
static void tap(int x,int y){sample(true,x,y);sample(false,x,y);advance(40);}
static void open() {
    sample(true,120,5);sample(true,121,20);sample(true,122,45);
    CHECK(ui::QuickPanel::visible());sample(false,122,45);CHECK(fake::panel);
}
static void closeSwipe() {
    sample(true,120,231);sample(true,121,213);sample(true,122,191);
    CHECK(!ui::QuickPanel::visible());sample(false,122,191);CHECK(!fake::panel);advance(40);
}
static void geometry(lv_obj_t *obj,std::vector<int> &out) {
    lv_area_t a;lv_obj_get_coords(obj,&a);
    CHECK(a.x1>=0 && a.y1>=0 && a.x2<240 && a.y2<240);
    out.insert(out.end(),{a.x1,a.y1,a.x2,a.y2});
    if(lv_obj_check_type(obj,&lv_label_class)) {
        CHECK(lv_obj_get_height(obj)==lv_obj_get_style_text_font(obj,LV_PART_MAIN)->line_height);
        uint32_t offset=0;const auto label=lv_label_get_text(obj);uint32_t ch;
        while((ch=lv_text_encoded_next(label,&offset))) {
            lv_font_glyph_dsc_t d{};
            CHECK(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(obj,LV_PART_MAIN),&d,ch,0) && !d.is_placeholder);
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)geometry(lv_obj_get_child(obj,i),out);
}
int main() {
    lv_init();auto d=lv_display_create(240,240);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
    auto base=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(base);lv_obj_set_size(base,240,240);
    lv_obj_set_scrollable(base,false);lv_obj_set_gesture_bubble(base,false);
    lv_obj_add_event_cb(base,[](lv_event_t *e){switch(lv_event_get_code(e)) {
        case LV_EVENT_CLICKED:++clicks;break;case LV_EVENT_GESTURE:++gestures;break;case LV_EVENT_PRESS_LOST:++lost;break;default:break;
    }},LV_EVENT_ALL,nullptr);
    lv_obj_update_layout(lv_screen_active());
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *data){data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;data->point={px,py};});
    ui::QuickPanel::init(touch);
    // Taps, excluded scene swipes and non-edge reader gestures remain usable.
    tap(120,5);CHECK(clicks==1);
    fake::allowed=false;
    for(int i=0;i<5;++i){sample(true,120,5);sample(true,120,25);sample(true,120,65);CHECK(!ui::QuickPanel::visible());sample(false,120,65);}
    CHECK(gestures>0);fake::allowed=true;
    const auto before=clicks,gBefore=gestures;
    open();CHECK(clicks==before && gestures==gBefore && lost>0);
    closeSwipe();
    std::vector<int> layout;
    for(int id:{4,5,6,8,10}) {
        ui::theme::init(id);open();std::vector<int> next;geometry(ui::QuickPanel::root(),next);
        if(layout.empty())layout=next;else CHECK(next==layout);
        CHECK(lv_obj_get_style_radius(ui::tiles[0],LV_PART_MAIN)==6);
        char file[60];std::snprintf(file,sizeof(file),"theme-%02d.ppm",id);capture(file);
        // Exercise actual pointer input at both ends, including the widest label.
        for(int end=0;end<2;++end) {
            for(unsigned row=0;row<2;++row) {
                auto slider=row?ui::volume:ui::brightness;
                lv_area_t track;lv_obj_get_coords(slider,&track);
                const int y=(track.y1+track.y2)/2;
                const int x=end?track.x2+1:track.x1-1;
                sample(true,(track.x1+track.x2)/2,y);sample(true,x,y);sample(false,x,y);
                const int expected=end?100:row?0:5;
                CHECK((row?fake::vol:fake::bright)==expected);
                CHECK(lv_slider_get_value(slider)==expected);
                char percent[12];std::snprintf(percent,sizeof(percent),"%d%%",expected);
                CHECK(std::strcmp(lv_label_get_text(ui::values[row]),percent)==0);
                lv_refr_now(nullptr);
                lv_area_t icon,label;
                lv_obj_get_coords(lv_obj_get_child(lv_obj_get_parent(slider),0),&icon);
                lv_obj_get_coords(ui::values[row],&label);
                const auto knob=reinterpret_cast<lv_slider_t *>(slider)->right_knob_area;
                if(knob.x1<=icon.x2 || knob.x2>=label.x1) {
                    std::fprintf(stderr,"theme %d row %u value %d: icon %d..%d, knob %d..%d, label %d..%d\n",id,row,expected,icon.x1,icon.x2,knob.x1,knob.x2,label.x1,label.x2);
                    capture("slider-spacing-failure.ppm");
                }
                CHECK(knob.x1>icon.x2 && knob.x2<label.x1);
                lv_point_t size;
                lv_text_get_size_internal(&size,percent,lv_obj_get_style_text_font(ui::values[row],LV_PART_MAIN),0,0,240,LV_TEXT_FLAG_NONE);
                CHECK(size.x<=lv_obj_get_content_width(ui::values[row]));
                CHECK(ui::QuickPanel::visible());
            }
            std::snprintf(file,sizeof(file),"theme-%02d-%s.ppm",id,end?"max":"min");capture(file);
        }
        fake::bright=35;fake::vol=50;
        closeSwipe();CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    }
    open();const int old=fake::bright;
    sample(true,80,52);sample(true,160,52);sample(false,160,52);CHECK(fake::bright>old);CHECK(ui::QuickPanel::visible());
    CHECK(!ui::QuickPanel::allowEvent({sys::EventType::NextPage}));
    CHECK(ui::QuickPanel::allowEvent({sys::EventType::WifiConnected}));
    int calls=fake::requests;tap(42,145);CHECK(fake::requests==calls+1 && fake::requested==sys::ServiceAction::WifiEnable && fake::requestedValue);
    fake::wifi.enabled=true;fake::wifi.state=sys::WifiState::Connected;advance(280);CHECK(lv_obj_has_state(ui::tiles[0],LV_STATE_CHECKED));
    fake::busy[0]=true;advance(280);calls=fake::requests;tap(42,145);CHECK(fake::requests==calls);fake::busy[0]=false;
    fake::hotspot=true;advance(280);tap(42,145);CHECK(fake::requests==calls);fake::hotspot=false;
    fake::bt.enabled=true;fake::bt.peer.connected=true;advance(280);CHECK(lv_obj_has_state(ui::tiles[1],LV_STATE_CHECKED));
    tap(120,145);CHECK(fake::requested==sys::ServiceAction::BluetoothEnable && !fake::requestedValue);
    // A finger can travel upward inside a tall tile without leaving it.
    // This must remain a button tap, not close the whole quick panel.
    calls=fake::requests;
    sample(true,120,163);sample(true,121,145);sample(true,122,130);sample(false,122,130);
    CHECK(ui::QuickPanel::visible()&&fake::requests==calls+1);
    // Repeat upward-moving taps, including one straddling the 250 ms refresh.
    for(int i=0;i<20;++i) {
        calls=fake::requests;sample(true,120,161);advance(0);
        sample(true,120,149);sample(true,120,132);sample(false,120,132);
        CHECK(ui::QuickPanel::visible()&&fake::requests==calls+1);
    }
    tap(198,145);CHECK(fake::requested==sys::ServiceAction::Motion && fake::requestedValue);
    fake::storage=false;advance(280);tap(60,198);CHECK(!ui::dialog && fake::usbCalls==0);fake::storage=true;advance(280);
    for(auto busy:{&fake::mp3Busy,&fake::scanBusy}) {
        *busy=true;advance(280);tap(60,198);CHECK(!ui::dialog&&fake::usbCalls==0);*busy=false;
    }
    advance(280);
    sample(true,60,213);sample(true,60,198);sample(true,60,181);sample(false,60,181);
    CHECK(ui::dialog&&ui::QuickPanel::visible());tap(60,214);CHECK(!ui::dialog);
    tap(60,198);CHECK(ui::dialog);capture("usb-confirm.ppm");tap(60,214);CHECK(!ui::dialog && fake::usbCalls==0);
    tap(60,198);tap(180,214);CHECK(fake::usbCalls==1 && fake::usb && !ui::dialog);
    fake::usbBusy=false;advance(280);
    tap(180,198);CHECK(ui::dialog);calls=fake::requests;tap(180,166);CHECK(fake::requests==calls);ui::QuickPanel::back();
    tap(60,198);tap(180,214);CHECK(fake::usbCalls==2 && !fake::usb);
    fake::usbBusy=false;fake::usbIdle=true;advance(280);
    tap(180,198);capture("sleep-confirm.ppm");tap(180,166);CHECK(ui::deepWaiting && fake::requested==sys::ServiceAction::DeepSleep);
    CHECK(ui::QuickPanel::back() && ui::deepWaiting);
    fake::results[2]={sys::ServiceAction::DeepSleep,ESP_ERR_INVALID_STATE,1};advance(280);CHECK(!ui::deepWaiting && ui::dialog);
    ui::QuickPanel::back();tap(180,198);tap(60,166);CHECK(!fake::screen && !ui::QuickPanel::visible());
    fake::screen=true;advance(40);open();
    fake::pairing=lv_obj_create(lv_layer_top());ui::QuickPanel::poll();CHECK(!ui::QuickPanel::visible());
    lv_obj_delete(fake::pairing);fake::pairing=nullptr;advance(40);
    // Repeated creation/deletion and app navigation during a held gesture.
    for(int i=0;i<100;++i){open();closeSwipe();CHECK(lv_obj_get_child_count(lv_layer_top())==0);}
    sample(true,120,5);ui::QuickPanel::close();sample(true,120,60);CHECK(!ui::QuickPanel::visible());sample(false,120,60);
    tap(120,5);CHECK(clicks>before);
    std::printf("Quick panel: %u checks; real LVGL pointer gestures, five palettes, UI geometry, async failures and 100 lifecycle cycles passed\n",checks);
    lv_display_delete(d);lv_deinit();return 0;
}
