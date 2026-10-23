#include "external_keyboard.hpp"
#include "bluetooth_pairing.hpp"
#include "window_mgr.hpp"
#include "quick_panel.hpp"
#include "text_input.hpp"
#include "theme.hpp"
#include "core/keyboard_input.hpp"
#include "core/ble_hid.hpp"
#include "core/ble_keyboard_client.hpp"
#include "core/power_mgr.hpp"
#include "core/event_bus.hpp"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <new>
#include <array>
#include <algorithm>
namespace ui {
namespace {
lv_indev_t *input=nullptr;lv_group_t *group=nullptr;
ExternalKeyboard::RawHandler rawHandler=nullptr;void *rawContext=nullptr;
struct Key { uint32_t code=0;bool down=false; };
struct Workspace {
    std::array<lv_obj_t *,128> controls{};
    std::array<Key,128> events{};
    std::array<uint32_t,256> held{};
};
// GUI-only, bounded boot-lifetime storage. Keep controller/DMA RAM available.
Workspace *workspace=nullptr;
unsigned controlCount=0,head=0,count=0;sys::hidinput::Keys previous;
bool linked=false,caps=false,ignoreUntilRelease=false;uint32_t lastCode=0,nextFocus=0;
void enqueue(uint32_t code,bool down) {
    if(!code) return;
    if(count==workspace->events.size()) { head=count=0;workspace->held={};previous={};lv_indev_reset(input,nullptr);return; }
    workspace->events[(head+count)%workspace->events.size()]={code,down};++count;
}
void read(lv_indev_t *,lv_indev_data_t *data) {
    if(count) { const auto event=workspace->events[head];head=(head+1)%workspace->events.size();--count;lastCode=event.code;data->state=event.down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED; }
    else data->state=LV_INDEV_STATE_RELEASED;
    data->key=lastCode;data->continue_reading=count!=0;
}
void gather(lv_obj_t *root,std::array<lv_obj_t *,128> &out,unsigned &n) {
    if(!root || n==out.size() || lv_obj_is_hidden(root) || lv_obj_has_state(root,LV_STATE_DISABLED)) return;
    if(lv_obj_is_clickable(root) && (lv_obj_check_type(root,&lv_button_class) || lv_obj_check_type(root,&lv_textarea_class) ||
       lv_obj_check_type(root,&lv_dropdown_class) || lv_obj_check_type(root,&lv_slider_class) ||
       lv_obj_check_type(root,&lv_switch_class) || lv_obj_check_type(root,&lv_checkbox_class) ||
       lv_obj_check_type(root,&lv_roller_class))) out[n++]=root;
    for(uint32_t i=0;i<lv_obj_get_child_count(root) && n<out.size();++i) gather(lv_obj_get_child(root,i),out,n);
}
lv_obj_t *modalScope(lv_obj_t *root) {
    if(!root) return nullptr;
    for(int32_t i=static_cast<int32_t>(lv_obj_get_child_count(root))-1;i>=0;--i) {
        auto child=lv_obj_get_child(root,i);if(lv_obj_is_hidden(child)) continue;
        if(lv_obj_get_user_flag(child,0)) return child;
    }
    return root;
}
void updateFocus() {
    auto root=BluetoothPairing::root();if(!root) root=TextInput::root();
    if(!root) root=WindowManager::instance().inputRoot();
    std::array<lv_obj_t *,128> next{};unsigned size=0;gather(modalScope(root),next,size);
    if(lv_group_get_obj_count(group)==size && size==controlCount && std::equal(next.begin(),next.begin()+size,workspace->controls.begin())) return;
    auto focused=lv_group_get_focused(group);
    // A queued click must never migrate to a replacement page/control.
    lv_indev_reset(input,nullptr);head=count=0;lastCode=0;lv_group_set_editing(group,false);
    lv_group_remove_all_objs(group);
    for(unsigned i=0;i<size;++i) {
        lv_group_add_obj(group,next[i]);
        lv_obj_set_style_outline_width(next[i],2,LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(next[i],-2,LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_color(next[i],theme::accent(),LV_STATE_FOCUS_KEY);
    }
    workspace->controls=next;controlCount=size;
    if(focused && std::find(next.begin(),next.begin()+size,focused)!=next.begin()+size) lv_group_focus_obj(focused);
}
uint32_t translate(unsigned key,bool shift) {
    if(key>=4 && key<=29) return (shift!=caps?'A':'a')+key-4;
    if(key>=30 && key<=39) return (shift?"!@#$%^&*()":"1234567890")[key-30];
    switch(key) {
    case 0x28:case 0x58:return LV_KEY_ENTER;
    case 0x29:return LV_KEY_ESC;
    case 0x2a:return LV_KEY_BACKSPACE;
    case 0x2b:return shift?LV_KEY_PREV:LV_KEY_NEXT;
    case 0x2c:return ' ';
    case 0x2d:return shift?'_':'-';case 0x2e:return shift?'+':'=';
    case 0x2f:return shift?'{':'[';case 0x30:return shift?'}':']';
    case 0x31:case 0x32:return shift?'|':'\\';case 0x33:return shift?':':';';
    case 0x34:return shift?'"':'\'';case 0x35:return shift?'~':'`';
    case 0x36:return shift?'<':',';case 0x37:return shift?'>':'.';case 0x38:return shift?'?':'/';
    case 0x4a:return LV_KEY_HOME;case 0x4d:return LV_KEY_END;case 0x4c:return LV_KEY_DEL;
    case 0x4f:return LV_KEY_RIGHT;case 0x50:return LV_KEY_LEFT;case 0x51:return LV_KEY_DOWN;case 0x52:return LV_KEY_UP;
    case 0x54:return '/';case 0x55:return '*';case 0x56:return '-';case 0x57:return '+';
    case 0x59:case 0x5a:case 0x5b:case 0x5c:case 0x5d:case 0x5e:case 0x5f:case 0x60:case 0x61:return '1'+key-0x59;
    case 0x62:return '0';case 0x63:return '.';
    default:return 0;
    }
}
unsigned repeatUsage=0;uint32_t repeatAt=0;
uint32_t controllerCode(unsigned usage) {
    switch(usage) {
    case 0x04:case 0x28:return LV_KEY_ENTER; // Captured H-mode A, plus letter-mode alias.
    case 0x05:case 0x2c:return LV_KEY_ESC;   // Captured H-mode B.
    case 0x50:return LV_KEY_LEFT;case 0x4f:return LV_KEY_RIGHT;
    case 0x52:return LV_KEY_UP;case 0x51:return LV_KEY_DOWN;
    default:return 0;
    }
}
lv_dir_t direction(uint32_t code) {
    switch(code) {
    case LV_KEY_LEFT:return LV_DIR_LEFT;case LV_KEY_RIGHT:return LV_DIR_RIGHT;
    case LV_KEY_UP:return LV_DIR_TOP;case LV_KEY_DOWN:return LV_DIR_BOTTOM;
    default:return LV_DIR_NONE;
    }
}
void moveFocus(lv_dir_t dir) {
    auto focused=lv_group_get_focused(group);if(!focused) return;
    lv_obj_update_layout(focused);
    lv_area_t from;lv_obj_get_coords(focused,&from);
    const bool horizontal=dir==LV_DIR_LEFT || dir==LV_DIR_RIGHT;
    const bool forward=dir==LV_DIR_RIGHT || dir==LV_DIR_BOTTOM;
    lv_obj_t *best=nullptr;bool bestAligned=false;int64_t bestScore=INT64_MAX;
    for(unsigned i=0;i<controlCount;++i) {
        auto obj=workspace->controls[i];if(obj==focused) continue;
        lv_area_t to;lv_obj_get_coords(obj,&to);if(to.x2<to.x1 || to.y2<to.y1) continue;
        const int64_t dx=int64_t(to.x1)+to.x2-from.x1-from.x2;
        const int64_t dy=int64_t(to.y1)+to.y2-from.y1-from.y2;
        const auto along=(horizontal?dx:dy)*(forward?1:-1);
        if(along<=0) continue;
        const auto across=horizontal?dy:dx;
        const bool aligned=horizontal?(to.y1<=from.y2 && to.y2>=from.y1):(to.x1<=from.x2 && to.x2>=from.x1);
        const auto score=along*along+2*across*across;
        if(!best || (aligned && !bestAligned) || (aligned==bestAligned && score<bestScore)) {
            best=obj;bestAligned=aligned;bestScore=score;
        }
    }
    if(best) { lv_group_focus_obj(best);lv_obj_scroll_to_view_recursive(best,LV_ANIM_OFF); }
}
bool adjustable(lv_obj_t *obj) {
    return obj && (lv_obj_check_type(obj,&lv_slider_class) || lv_obj_check_type(obj,&lv_roller_class));
}
void controllerAction(uint32_t code) {
    updateFocus();
    lv_display_trigger_activity(lv_display_get_default());
    auto focus=lv_group_get_focused(group);
    const bool dropdown=focus && lv_obj_check_type(focus,&lv_dropdown_class) && lv_dropdown_is_open(focus);
    if(code==LV_KEY_ESC) {
        if(dropdown) { enqueue(LV_KEY_ESC,true);enqueue(LV_KEY_ESC,false); }
        else if(adjustable(focus) && lv_group_get_editing(group)) {
            lv_group_set_editing(group,false);
            enqueue(LV_KEY_ENTER,true);enqueue(LV_KEY_ENTER,false); // Commit release-driven controls too.
        }
        else if(!BluetoothPairing::back()) sys::EventBus::publish({sys::EventType::BackRequested});
        return;
    }
    if(code==LV_KEY_ENTER) {
        if(!focus) return;
        if(lv_obj_check_type(focus,&lv_textarea_class)) { TextInput::show(focus);return; }
        if(adjustable(focus)) {
            const bool editing=lv_group_get_editing(group);lv_group_set_editing(group,!editing);
            if(!editing) return;
        }
        enqueue(LV_KEY_ENTER,true);enqueue(LV_KEY_ENTER,false);return;
    }
    if(dropdown || (adjustable(focus) && lv_group_get_editing(group))) {
        enqueue(code,true);enqueue(code,false);return;
    }
    const auto dir=direction(code);
    if(dir==LV_DIR_NONE) return;
    if(!BluetoothPairing::root() && !QuickPanel::visible() && !TextInput::visible() &&
       WindowManager::instance().directionalKey(dir)) return;
    moveFocus(dir);
}
bool applyController(const sys::hidinput::Keys &keys) {
    if(repeatUsage && !keys.down(repeatUsage)) repeatUsage=0;
    const auto old=previous;previous=keys;
    // One action per report: Back/Confirm take priority over a diagonal/chord.
    for(unsigned usage:{0x2cu,0x05u,0x28u,0x04u,0x52u,0x51u,0x50u,0x4fu}) {
        if(!keys.down(usage) || old.down(usage)) continue;
        const auto code=controllerCode(usage);
        repeatUsage=direction(code)!=LV_DIR_NONE?usage:0;repeatAt=lv_tick_get()+500;
        controllerAction(code);return true;
    }
    return false;
}
void apply(const sys::hidinput::Keys &keys) {
    const bool shift=keys.down(0xe1)||keys.down(0xe5);
    const bool command=keys.down(0xe0)||keys.down(0xe4)||keys.down(0xe2)||keys.down(0xe3)||keys.down(0xe6)||keys.down(0xe7);
    if(command) repeatUsage=0;
    for(unsigned key=4;key<0xe0;++key) if(previous.down(key) && !keys.down(key)) {
        enqueue(workspace->held[key],false);workspace->held[key]=0;if(repeatUsage==key) repeatUsage=0;
    }
    for(unsigned key=4;key<0xe0;++key) if(keys.down(key) && !previous.down(key)) {
        if(key==0x39) { caps=!caps;continue; }
        auto code=translate(key,shift);auto focus=lv_group_get_focused(group);
        if(code==LV_KEY_ESC) {
            if(focus && lv_obj_check_type(focus,&lv_dropdown_class) && lv_dropdown_is_open(focus)) lv_dropdown_close(focus);
            else sys::EventBus::publish({sys::EventType::BackRequested});
            continue;
        }
        if(command) continue; // do not turn Ctrl/Alt/GUI shortcuts into stray text
        if((code==LV_KEY_UP || code==LV_KEY_DOWN || code==LV_KEY_LEFT || code==LV_KEY_RIGHT) &&
           (!focus || (!lv_obj_check_type(focus,&lv_textarea_class) && !lv_obj_check_type(focus,&lv_slider_class) && !lv_obj_check_type(focus,&lv_dropdown_class))))
            code=(code==LV_KEY_UP || code==LV_KEY_LEFT)?LV_KEY_PREV:LV_KEY_NEXT;
        workspace->held[key]=code;enqueue(code,true);enqueue(code,false);
        if(code && code!=LV_KEY_ENTER) { repeatUsage=key;repeatAt=lv_tick_get()+500; }
    }
    previous=keys;
}
}
void ExternalKeyboard::init() {
    if(input) return;
    if(!workspace) {
        void *memory=heap_caps_malloc(sizeof(Workspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!memory) { ESP_LOGE("external_keyboard","Cannot allocate input workspace");return; }
        workspace=new(memory) Workspace{};
    }
    group=lv_group_create();lv_group_set_wrap(group,true);
    lv_group_set_focus_cb(group,[](lv_group_t *g) {
        // LVGL otherwise prefers the touch indev when focus moves in a timer.
        if(auto obj=lv_group_get_focused(g); obj && sys::BleKeyboardClient::iineKeyboard())
            lv_obj_add_state(obj,LV_STATE_FOCUS_KEY);
    });
    input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_KEYPAD);lv_indev_set_read_cb(input,read);lv_indev_set_group(input,group);
}
void ExternalKeyboard::resetFocus() {
    rawHandler=nullptr;rawContext=nullptr;
    rescope();
}
void ExternalKeyboard::rescope() {
    if(!input) return;
    lv_indev_reset(input,nullptr);lv_group_remove_all_objs(group);
    lv_group_set_editing(group,false);
    workspace->controls={};controlCount=0;head=count=0;workspace->held={};previous={};repeatUsage=0;nextFocus=0;
    lastCode=0;
    sys::hidinput::Keys discarded;
    for(unsigned i=0;i<32 && sys::KeyboardInput::pop(discarded);++i) {}
    uint32_t revision=0;const auto keys=sys::KeyboardInput::snapshot(revision);
    ignoreUntilRelease=std::any_of(keys.bits.begin(),keys.bits.end(),[](uint8_t b){return b!=0;});
}
void ExternalKeyboard::capture(RawHandler handler,void *context) {
    resetFocus();rawHandler=handler;rawContext=context;
}
void ExternalKeyboard::poll() {
    if(!input) return;
    const bool ready=sys::BleHid::keyboardConnected() && !sys::Power::sleepPending();
    if(!ready) {
        if(rawHandler) rawHandler({},rawContext);
        if(linked) { lv_indev_reset(input,nullptr);lv_group_remove_all_objs(group);workspace->controls={};controlCount=0; }
        linked=false;head=count=0;workspace->held={};previous={};repeatUsage=0;caps=false;ignoreUntilRelease=false;
        sys::hidinput::Keys discarded;for(unsigned i=0;i<32 && sys::KeyboardInput::pop(discarded);++i) {}
        return;
    }
    const bool controller=sys::BleKeyboardClient::iineKeyboard();
    if(!linked) {
        TextInput::hide();nextFocus=0;linked=true;
        if(controller) {
            uint32_t revision=0;const auto held=sys::KeyboardInput::snapshot(revision);
            ignoreUntilRelease=std::any_of(held.bits.begin(),held.bits.end(),[](uint8_t v){return v!=0;});
        }
    }
    const bool raw=rawHandler && !controller && !QuickPanel::visible() && !BluetoothPairing::root();
    if(!sys::Power::isScreenOn()) { lv_indev_reset(input,nullptr);head=count=0;repeatUsage=0; }
    // Let LVGL finish both halves of A before processing a later direction.
    if(controller && count) return;
    const auto now=lv_tick_get();if(!raw && now>=nextFocus) { nextFocus=now+100;updateFocus(); }
    sys::hidinput::Keys keys;
    for(unsigned i=0;i<32 && sys::KeyboardInput::pop(keys);++i) {
        const bool any=std::any_of(keys.bits.begin(),keys.bits.end(),[](uint8_t v){return v!=0;});
        if(!sys::Power::isScreenOn()) {
            head=count=0;workspace->held={};previous={};repeatUsage=0;
            if(any) { sys::Power::screenOn();ignoreUntilRelease=true; }
            continue;
        }
        if(ignoreUntilRelease) { if(!any) ignoreUntilRelease=false;continue; }
        if(controller) { if(applyController(keys)) break; }
        else if(raw) rawHandler(keys,rawContext);
        else apply(keys);
    }
    if(!sys::Power::isScreenOn()) { repeatUsage=0;return; }
    if(!raw && repeatUsage && static_cast<int32_t>(now-repeatAt)>=0) {
        if(controller) {
            uint32_t revision=0;const auto held=sys::KeyboardInput::snapshot(revision);
            if(!held.down(repeatUsage)) { repeatUsage=0;return; }
            controllerAction(controllerCode(repeatUsage));
        }
        else { enqueue(workspace->held[repeatUsage],true);enqueue(workspace->held[repeatUsage],false); }
        repeatAt=now+80;
    }
}
}
