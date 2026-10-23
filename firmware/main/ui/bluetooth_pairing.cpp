#include "bluetooth_pairing.hpp"
#include "bluetooth_identity.hpp"
#include "text_input.hpp"
#include "core/system_service.hpp"
#include "core/power_mgr.hpp"
#include <cstdio>
namespace ui {
namespace {
lv_obj_t *overlay=nullptr,*status=nullptr;
BluetoothIdentity identity;
uint32_t generation=0,answered=0,nextPoll=0;
void close() { if(overlay) lv_obj_delete(overlay);overlay=status=nullptr;identity={}; }
void submit(bool accept) {
    const auto result=sys::SystemService::answerPairing(accept,generation);
    if(result==ESP_OK) { answered=generation;close(); }
    else compact::setText(status,"操作未提交，请重试");
}
void answer(lv_event_t *e) { submit(lv_event_get_user_data(e)!=nullptr); }
}
lv_obj_t *BluetoothPairing::root() { return overlay; }
bool BluetoothPairing::back() {
    if(!overlay) return false;
    submit(false);return true;
}
void BluetoothPairing::poll() {
    if(sys::Power::sleepPending()) { close();return; }
    const auto now=lv_tick_get();if(static_cast<int32_t>(now-nextPoll)<0) return;nextPoll=now+100;
    const auto state=sys::SystemService::bluetooth();
    if(!state.enabled || state.pairing==sys::BlePairAction::None || state.pairGeneration==answered) { close();return; }
    if(overlay && generation!=state.pairGeneration) close();
    if(!overlay) {
        generation=state.pairGeneration;TextInput::hide();sys::Power::screenOn();
        overlay=compact::panel(lv_layer_top(),240,240);lv_obj_set_user_flag(overlay,0,true);
        auto title=compact::label(overlay,"蓝牙配对",238);lv_obj_set_pos(title,1,8);
        identity.create(overlay,238,1,36);
        status=compact::label(overlay,state.pairing==sys::BlePairAction::Confirm?"核对数字一致后确认":state.peer.keyboard?"在外置键盘输入数字，再按回车":"请在对方输入下方数字",238);
        lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_pos(status,1,80);lv_obj_set_height(status,44);
        char code[7];std::snprintf(code,sizeof(code),"%06lu",static_cast<unsigned long>(state.passkey%1000000u));
        auto passkey=compact::label(overlay,code);lv_obj_set_style_text_font(passkey,&lv_font_montserrat_24,0);lv_obj_align(passkey,LV_ALIGN_TOP_MID,0,132);
        auto no=compact::button(overlay,"取消",answer,nullptr,119,40);lv_obj_set_pos(no,0,200);
        auto yes=compact::button(overlay,state.pairing==sys::BlePairAction::Confirm?"确认":"知道了",answer,reinterpret_cast<void *>(1),120,40);lv_obj_set_pos(yes,120,200);
    }
    identity.update(state.peer);
}
}
