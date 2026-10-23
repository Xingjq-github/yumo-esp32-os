#include "settings.hpp"
#include "ui/compact_ui.hpp"
#include "theme.hpp"
#include "core/ble_peer_store.hpp"
#include "esp_mac.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
namespace apps {
namespace c=ui::compact;
namespace {
const char *bleStateText(const sys::BluetoothSnapshot &s) {
    if(!s.enabled) return "蓝牙已关闭";
    if(s.switching) return "正在结束旧连接…";
    if(s.discovering()) return "等待手机 / 电脑配对…";
    if(s.connecting()) return s.target.keyboard?"正在连接指定输入设备…":"等待所选设备连接…";
    switch(s.state) {
    case sys::BleHidState::Starting:return "蓝牙启动中…";
    case sys::BleHidState::Advertising:return "等待手机 / 电脑发现…";
    case sys::BleHidState::Connecting:return s.waitingForPeer?"等待对方连接…":s.peer.keyboard?"连接输入设备 / 配对中…":"正在配对 / 验证连接…";
    case sys::BleHidState::Connected:return s.peer.keyboard?"输入设备已连接 · 控制本机":"已连接 · 控制对方";
    case sys::BleHidState::Failed:return "连接失败，请重试";
    default:return s.scan==sys::BleScanState::Scanning?"正在查找附近设备…":"未连接";
    }
}
bool sameBond(const sys::BleBond &a,const sys::BleBond &b) { return sys::BlePeerStore::same(a,b); }
}
void Settings::buildBluetooth() {
    const auto state=sys::SystemService::bluetooth();
    v.toggle=c::toggle(v.body,"蓝牙",state.enabled,btToggleCb,this);
    auto actions=c::panel(v.body,240,36);lv_obj_set_flex_flow(actions,LV_FLEX_FLOW_ROW);
    v.discoverButton=c::button(actions,"允许发现",discoverCb,this,119,36);
    auto nearby=c::button(actions,"附近设备",pageCb,this,120,36);
    lv_obj_set_user_data(nearby,reinterpret_cast<void *>(static_cast<intptr_t>(Page::BluetoothScan)));
    v.connect=c::button(v.body,"断开当前连接",[](lv_event_t *e){static_cast<Settings *>(lv_event_get_user_data(e))->request(sys::ServiceAction::BluetoothDisconnect);},this,240,36);
    c::section(v.body,"已配对设备");v.list=c::panel(v.body,240,LV_SIZE_CONTENT);lv_obj_set_flex_flow(v.list,LV_FLEX_FLOW_COLUMN);refreshBonds(state);
    v.forget=c::button(v.body,"清除全部配对",forgetAllCb,this,240,36);
    v.status=c::label(v.body,"",238);lv_obj_set_height(v.status,24);
    auto active=c::panel(v.body,240,34);v.peerIdentity.create(active);
    c::section(v.body,"本机");auto own=c::panel(v.body,240,34);ui::BluetoothIdentity identity;identity.create(own);
    sys::BleBond self;std::snprintf(self.name,sizeof(self.name),"%s",sys::BleHid::deviceName());uint8_t mac[6]{};
    if(esp_read_mac(mac,ESP_MAC_BT)==ESP_OK) {
        for(unsigned i=0;i<6;++i) self.addr[i]=mac[5-i];
    }
    identity.update(self,sys::BleHid::deviceName());
    auto help=c::label(v.body,"本机点允许发现，再在对方蓝牙设置中选择本机。一次连接1台；手柄/键盘用附近搜索。",238);lv_label_set_long_mode(help,LV_LABEL_LONG_MODE_WRAP);
}
void Settings::btToggleCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));self->request(sys::ServiceAction::BluetoothEnable,lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED));
}
void Settings::discoverCb(lv_event_t *e) { static_cast<Settings *>(lv_event_get_user_data(e))->request(sys::ServiceAction::BluetoothDiscover,!sys::SystemService::bluetooth().discovering()); }
void Settings::refreshBonds(const sys::BluetoothSnapshot &state) {
    if(!v.list || listPressed) return;
    lv_obj_clean(v.list);bondCount=state.bondCount;
    for(int i=0;i<bondCount;++i) {
        bonds[i]=state.bonds[i];auto row=c::button(v.list,"",bondCb,this,240,42);
        lv_obj_set_user_data(row,reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        ui::BluetoothIdentity identity;identity.create(row,176,1,2);identity.update(bonds[i]);
        auto status=c::label(row,bonds[i].connected?"已连接":bonds[i].keyboard?"输入设备":LV_SYMBOL_RIGHT,60);
        lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_RIGHT,0);lv_obj_align(status,LV_ALIGN_RIGHT_MID,0,0);markListTouch(row);
    }
    if(!bondCount) c::label(v.list,state.enabled?"暂无配对记录":"开启蓝牙查看配对记录",238);
    bondRevision=state.bondRevision;
}
void Settings::bondCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));const auto index=reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    if(index<0 || index>=self->bondCount) return;
    self->selectedBond=self->bonds[index];self->navigate(Page::BluetoothDevice);
}
void Settings::buildBluetoothDevice() {
    v.connect=c::button(v.body,"连接",[](lv_event_t *e) {
        auto self=static_cast<Settings *>(lv_event_get_user_data(e));
        const auto state=sys::SystemService::bluetooth();
        if((state.peer.connected && sameBond(state.peer,self->selectedBond)) ||
           (state.connecting() && sameBond(state.target,self->selectedBond))) self->request(sys::ServiceAction::BluetoothDisconnect);
        else self->report(sys::SystemService::reconnectBond(self->selectedBond));
    },this,240,36);
    v.forget=c::button(v.body,"忘记此设备",forgetBondCb,this,240,36);
    auto identity=c::panel(v.body,240,36);v.peerIdentity.create(identity);v.peerIdentity.update(selectedBond);
    v.status=c::label(v.body,"",238);lv_label_set_long_mode(v.status,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(v.status,44);
    auto help=c::label(v.body,selectedBond.keyboard?"先开启手柄或键盘配对通道，再点连接；可切换已配对设备。":"点击连接后等待60秒；未响应时在对方点连接，无需清除配对。",238);lv_label_set_long_mode(help,LV_LABEL_LONG_MODE_WRAP);
}
void Settings::forgetBondCb(lv_event_t *e) { static_cast<Settings *>(lv_event_get_user_data(e))->ask(Dialog::ForgetBond,"删除此配对？\n当前连接将断开。"); }
void Settings::forgetAllCb(lv_event_t *e) { static_cast<Settings *>(lv_event_get_user_data(e))->ask(Dialog::ForgetAll,"清除全部配对？\n对方也需忘记本机。"); }
void Settings::buildBluetoothScan() {
    v.scanButton=c::button(v.body,"扫描附近设备",btScanCb,this,240,36);
    v.list=c::panel(v.body,240,LV_SIZE_CONTENT);lv_obj_set_flex_flow(v.list,LV_FLEX_FLOW_COLUMN);
    v.status=c::label(v.body,"",238);lv_obj_set_height(v.status,44);lv_label_set_long_mode(v.status,LV_LABEL_LONG_MODE_WRAP);
    auto help=c::label(v.body,"搜索BLE手柄和键盘。手机和电脑请返回，点允许发现后配对。",238);lv_label_set_long_mode(help,LV_LABEL_LONG_MODE_WRAP);
    const auto state=sys::SystemService::bluetooth();
    if(state.enabled && state.state!=sys::BleHidState::Starting && state.scan!=sys::BleScanState::Scanning) request(sys::ServiceAction::BluetoothScan);
}
void Settings::btScanCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));
    if(sys::SystemService::bluetooth().scan==sys::BleScanState::Scanning) sys::SystemService::cancelBluetoothScan();
    else self->request(sys::ServiceAction::BluetoothScan);
}
void Settings::nearbyCb(lv_event_t *e) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(e));const auto index=reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    if(index<0 || index>=self->scanShown) return;
    const auto state=sys::SystemService::bluetooth();
    if(state.peer.connected || state.state==sys::BleHidState::Connecting) { self->notice("同时只能连接一个设备，请先断开当前连接");return; }
    self->report(sys::SystemService::connectKeyboard(self->scanned[index]),"正在连接，请留意配对提示");
}
void Settings::refreshBluetooth(bool force) {
    const auto state=sys::SystemService::bluetooth();const bool busy=sys::SystemService::busy(sys::ServiceDomain::Bluetooth);
    if(v.toggle) { if(!busy) c::checked(v.toggle,state.enabled);c::disabled(v.toggle,busy); }
    if(page==Page::Bluetooth) {
        char status[96];std::snprintf(status,sizeof(status),"%s",bleStateText(state));
        if(state.hasError() && state.error) std::snprintf(status,sizeof(status),"连接异常 rc=%d，请重试",state.error);
        c::setText(v.status,status);v.peerIdentity.update(state.peer.connected?state.peer:state.connecting()?state.target:sys::BleBond{});
        char discover[40];if(state.discoverableSeconds && state.discovering()) std::snprintf(discover,sizeof(discover),"发现 %lu 秒",static_cast<unsigned long>(state.discoverableSeconds));else std::snprintf(discover,sizeof(discover),"%s",state.discovering()?"取消发现":"允许发现");
        c::setText(lv_obj_get_child(v.discoverButton,0),discover);
        c::disabled(v.discoverButton,busy || !state.enabled || state.state==sys::BleHidState::Starting);
        c::setText(lv_obj_get_child(v.connect,0),state.cancellable()?"取消连接":"断开当前连接");
        c::disabled(v.connect,busy || (!state.peer.connected && !state.cancellable()));c::disabled(v.forget,busy || state.bondCount==0);
        if(force || bondRevision!=state.bondRevision) refreshBonds(state);
    } else if(page==Page::BluetoothDevice) {
        bool known=false,connected=false;
        for(int i=0;i<state.bondCount;++i) if(sameBond(selectedBond,state.bonds[i])) { known=true;connected=state.bonds[i].connected;selectedBond=state.bonds[i]; }
        const bool connecting=state.connecting() && sameBond(state.target,selectedBond);
        const bool failed=state.hasError() && sameBond(state.target,selectedBond);
        v.peerIdentity.update(selectedBond);
        c::setText(v.status,!known?"此配对已移除":!state.enabled?"蓝牙已关闭 · 配对已保存":connected?"已连接":failed?"连接失败，请重试":connecting?(state.switching?"正在结束旧连接…":selectedBond.keyboard?"正在查找 / 配对连接…":"等待对方连接（最多60秒）"):selectedBond.keyboard?"已保存，可查找并连接":"已保存，可请求连接");
        if(known && state.enabled && !connected && failed && state.error) {
            char error[64];std::snprintf(error,sizeof(error),"连接异常 rc=%d，请重试",state.error);c::setText(v.status,error);
        }
        c::setText(lv_obj_get_child(v.connect,0),connecting?"取消连接":connected?"断开连接":state.peer.connected || state.cancellable()?"切换至此设备":selectedBond.keyboard?"查找并连接":"请求连接");
        c::disabled(v.connect,busy || !known || !state.enabled || state.state==sys::BleHidState::Starting);
        c::disabled(v.forget,busy || !known);
    } else if(page==Page::BluetoothScan) {
        const bool scanning=state.scan==sys::BleScanState::Scanning;
        c::disabled(v.scanButton,busy || !state.enabled || state.state==sys::BleHidState::Starting || state.state==sys::BleHidState::Connecting);
        c::setText(lv_obj_get_child(v.scanButton,0),scanning?"停止扫描":"重新扫描");
        // Preserve pressed row identities; don't let a scan update retarget a click.
        if(!listPressed) {
            const int count=sys::SystemService::nearby(scanned.data(),scanned.size());
            if(count<scanShown) { lv_obj_clean(v.list);v.scanRows={};v.scanIdentity={};scanShown=0; }
            for(int i=0;i<count;++i) {
                if(!v.scanRows[i]) {
                    auto row=c::button(v.list,"",nearbyCb,this,240,42);v.scanRows[i]=row;
                    lv_obj_set_user_data(row,reinterpret_cast<void *>(static_cast<intptr_t>(i)));
                    v.scanIdentity[i].create(row,234,1,2);markListTouch(row);
                }
                auto device=scanned[i].address;std::snprintf(device.name,sizeof(device.name),"%s",scanned[i].name);
                v.scanIdentity[i].update(device);c::disabled(v.scanRows[i],busy || !state.enabled || !scanned[i].connectable || state.peer.connected || state.state==sys::BleHidState::Connecting);
            }
            scanShown=count;scanRevision=state.scanRevision;
        }
        char text[128];const char *title=state.hasError()?"连接失败或设备不支持":!state.enabled?"蓝牙已关闭":state.state==sys::BleHidState::Starting?"蓝牙启动中…":state.state==sys::BleHidState::Connecting?"连接 / 配对中…":state.peer.connected?bleStateText(state):scanning?"扫描中（最多10秒）":scanShown?"扫描完成":"没有发现设备，可重试";
        std::snprintf(text,sizeof(text),"%s\n%d 台设备 · 同时连接1台",title,scanShown);c::setText(v.status,text);
    }
}
} // namespace apps
