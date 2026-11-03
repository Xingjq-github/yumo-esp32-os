#include "bt_keyboard_mouse.hpp"
#include "core/ble_hid.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "core/system_service.hpp"
#include "ui/hid_controls.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
namespace apps {
namespace {
// 触摸区拖动灵敏度基准(放大手指位移 -> 光标位移); x2 时翻倍
constexpr float kMoveGainBase = 1.6f;
// 单击判定阈值:总位移小于该值视为点击而非拖动
constexpr int32_t kClickThreshold = 12;

// 指针加速(pointer acceleration):参考成熟触控板方案,增益随手指瞬时
// 速度非线性增大 —— 慢速滑动低增益(高精度微调),快速滑动高增益(快速跨屏)。
//   有效增益 = kMoveGainBase * (1 + kAccelFactor * speed)
//   speed 为本次事件的曼哈顿位移(|dx|+|dy|),再按 kAccelCap 限制上限。
constexpr float kAccelFactor = 0.06f; // 速度对增益的放大系数
constexpr float kAccelCap = 3.5f;     // 有效增益相对基准的最大倍数(防止过冲)

// 滚轮触摸条:每移动这么多像素发送 1 格滚轮
constexpr int32_t kWheelStep = 14;

// tap-and-drag(点击后拖拽)判定参数:
//   kTapMaxMs      : 一次按下-抬起在此时间内且位移小,才算作有效"轻点"(tap)
//   kDragWindowMs  : tap 抬起后,在此窗口内再次按下则进入拖拽锁定
//   kDragMoveStart : 拖拽待命时,二次按下后需移动超过此值才真正开始拖拽,
//                    防止把"想双击"误判成拖拽
constexpr uint32_t kTapMaxMs = 260;
constexpr uint32_t kDragWindowMs = 320;
constexpr int32_t kDragMoveStart = 4;

// 连击频率下拉选项(每秒点击次数)
constexpr int32_t kRateOptions[] = {1, 2, 5, 10};

} // namespace
void BtKeyboardMouse::onCreate(lv_obj_t *parent) {
    namespace c=ui::compact; namespace h=ui::hid;
    lv_obj_set_style_bg_color(parent,ui::theme::bg(),0);
    lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(parent,0,0);lv_obj_set_scrollable(parent,false);
    // Shared shell remains present in mouse, Win, Mac and multi-tap modes.
    m_switch=h::button(parent,LV_SYMBOL_KEYBOARD,switchCb,this,3,0,65,44);
    m_mouseBar=c::panel(parent,171,44);lv_obj_set_pos(m_mouseBar,69,0);
    auto right=h::button(m_mouseBar,"R",nullptr,nullptr,0,0,38,44);
    lv_obj_add_event_cb(right,clickRightCb,LV_EVENT_ALL,this);
    m_speedLabel=h::caption(h::button(m_mouseBar,"x1",speedCb,this,39,0,38,44));
    m_autoLabel=h::caption(h::button(m_mouseBar,"C",autoClickCb,this,78,0,48,44));
    auto dd=c::dropdown(m_mouseBar,"1/s\n2/s\n5/s\n10/s",44);
    lv_obj_set_pos(dd,127,0);lv_obj_set_height(dd,44);h::style(dd);
    // The selected rate fits the 44px wheel column, including 10/s.
    lv_obj_set_style_text_font(dd,&lv_font_montserrat_12,0);
    lv_dropdown_set_symbol(dd,nullptr);
    lv_obj_set_style_bg_color(lv_dropdown_get_list(dd),ui::theme::accent(),static_cast<lv_style_selector_t>(LV_PART_SELECTED)|static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
    lv_obj_add_event_cb(dd,rateCb,LV_EVENT_VALUE_CHANGED,this);
    m_tabsBar=c::panel(parent,171,44);lv_obj_set_pos(m_tabsBar,69,0);
    const char *tabs[]={"Win","Mac","Abc"};
    for(unsigned i=0;i<3;++i) {
        m_tabs[i]=h::button(m_tabsBar,tabs[i],tabCb,this,i*57,0,56,44);
        lv_obj_set_user_data(m_tabs[i],reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    // Mouse-only scroll strip. Keyboard grids consume its entire width.
    m_wheel=h::button(parent,LV_SYMBOL_UP "\n" LV_SYMBOL_DOWN,nullptr,nullptr,196,45,44,195);
    lv_obj_add_event_cb(m_wheel,wheelCb,LV_EVENT_ALL,this);
    m_pad=c::panel(parent,195,195);lv_obj_set_pos(m_pad,0,45);
    lv_obj_set_style_bg_color(m_pad,ui::theme::surface(),0);
    lv_obj_set_clickable(m_pad,true);
    lv_obj_add_event_cb(m_pad,touchpadCb,LV_EVENT_ALL,this);
    // No status prose; the original colored link rail carries connection state.
    m_keys=c::panel(parent,240,195);lv_obj_set_pos(m_keys,0,45);
    m_shortcuts=c::panel(m_keys,240,195);
    for(unsigned i=0;i<sizeof(keys::shortcuts)/sizeof(keys::shortcuts[0]);++i) {
        auto b=h::button(m_shortcuts,keys::shortcuts[i].label,shortcutCb,this,(i%3)*80,(i/3)*49,79,48);
        lv_obj_align(h::caption(b),LV_ALIGN_TOP_MID,0,1);
        auto hint=c::label(b,keys::shortcuts[i].winHint);
        lv_obj_set_style_text_font(hint,&lv_font_montserrat_10,0);
            lv_obj_align(hint,LV_ALIGN_BOTTOM_MID,0,-2);m_shortcutHints[i]=hint;
        lv_obj_set_user_data(b,reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    m_charPanel=c::panel(m_keys,240,195);
    m_preview=c::label(m_charPanel,"abc",240);lv_obj_set_pos(m_preview,0,0);
    lv_obj_set_style_text_color(m_preview,ui::theme::accent(),0);
    lv_obj_set_style_text_align(m_preview,LV_TEXT_ALIGN_CENTER,0);
    for(unsigned i=0;i<12;++i) {
        unsigned action=i<9?i+1:(i==9?10:i==10?0:11);
        auto b=h::button(m_charPanel," ",keyCb,this,(i%3)*80,31+(i/3)*41,79,40);
        lv_obj_set_user_data(b,reinterpret_cast<void *>(static_cast<uintptr_t>(action)));
        auto label=h::caption(b);
        if(action<10) { m_digitLabels[action]=label; lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0); }
        else if(action==10) m_modeLabel=label;
        else lv_label_set_text(label,LV_SYMBOL_BACKSPACE);
    }
    updateKeyLabels();selectTab(0);showKeyboard(false);
    m_status=lv_obj_create(parent);lv_obj_remove_style_all(m_status);
    lv_obj_set_pos(m_status,0,0);lv_obj_set_size(m_status,2,44);
    lv_obj_set_style_bg_opa(m_status,LV_OPA_COVER,0);lv_obj_set_clickable(m_status,false);
    m_screenOffSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &) {
        sys::BleHid::cancelKeys();cancelPending();releasePointer();
    });
    m_statusTimer=lv_timer_create(statusTimerCb,50,this);
}
void BtKeyboardMouse::onShow() { m_visible=true;m_nextStatusMs=0;if(m_statusTimer) lv_timer_resume(m_statusTimer);refreshStatus(); }
void BtKeyboardMouse::onHide() {
    m_visible=false;sys::BleHid::cancelKeys();cancelPending();releasePointer();m_autoClick=false;
    if(m_autoTimer) { lv_timer_delete(m_autoTimer);m_autoTimer=nullptr; }
    if(m_statusTimer) lv_timer_pause(m_statusTimer);
}
void BtKeyboardMouse::onDestroy() {
    onHide();sys::EventBus::unsubscribe(m_screenOffSub);
    if(m_statusTimer) { lv_timer_delete(m_statusTimer);m_statusTimer=nullptr; }
    if(m_autoTimer) { lv_timer_delete(m_autoTimer);m_autoTimer=nullptr; }
}
bool BtKeyboardMouse::onBack() { if(m_keyboard) { showKeyboard(false);return true; } return false; }
void BtKeyboardMouse::releasePointer() {
    // Cancellation drops unsent movement and clicks, while the transport
    // retains a mandatory all-up for the same live connection when needed.
    sys::BleHid::cancelMouse();
    m_dragging=m_dragArmed=m_touching=m_wheeling=false;m_accX=m_accY=0;
    m_pointerLink=m_clickLink=0;
    m_lastReleaseMs=lv_tick_get()-kDragWindowMs-1;
}
void BtKeyboardMouse::cancelPending() { if(m_compose.pending) { m_compose.clear();refreshPreview(); } }
void BtKeyboardMouse::showKeyboard(bool show) {
    sys::BleHid::cancelKeys();cancelPending();releasePointer();m_keyboard=show;
    ui::compact::checked(m_switch,show);
    if(show) lv_obj_set_hidden(m_wheel,true);
    else lv_obj_set_hidden(m_wheel,false);
    if(show) {
        lv_obj_set_hidden(m_mouseBar,true);lv_obj_set_hidden(m_pad,true);
        lv_obj_set_hidden(m_tabsBar,false);lv_obj_set_hidden(m_keys,false);
    } else {
        lv_obj_set_hidden(m_mouseBar,false);lv_obj_set_hidden(m_pad,false);
        lv_obj_set_hidden(m_tabsBar,true);lv_obj_set_hidden(m_keys,true);
    }
}
void BtKeyboardMouse::switchCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));self->showKeyboard(!self->m_keyboard);
}
void BtKeyboardMouse::selectTab(unsigned tab) {
    sys::BleHid::cancelKeys();cancelPending();m_tab=tab<3?tab:0;
    for(unsigned i=0;i<3;++i) ui::compact::checked(m_tabs[i],i==m_tab);
    for(unsigned i=0;i<12;++i)
        ui::compact::setText(m_shortcutHints[i],m_tab==1?keys::shortcuts[i].macHint:keys::shortcuts[i].winHint);
    if(m_tab==2) { lv_obj_set_hidden(m_shortcuts,true);lv_obj_set_hidden(m_charPanel,false); }
    else { lv_obj_set_hidden(m_shortcuts,false);lv_obj_set_hidden(m_charPanel,true); }
}
void BtKeyboardMouse::tabCb(lv_event_t *e) {
    static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e))->selectTab(static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)))));
}
void BtKeyboardMouse::shortcutCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));
    const auto i=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    if(!self->m_visible || !self->m_keyboard || self->m_tab>=2 || i>=sizeof(keys::shortcuts)/sizeof(keys::shortcuts[0])) return;
    const auto key=self->m_tab==1?keys::shortcuts[i].mac:keys::shortcuts[i].win;
    if(!sys::Power::isScreenOn() || sys::BleHid::sendKey(key.code,key.modifiers)!=ESP_OK) self->m_errorUntil=lv_tick_get()+800;
}
bool BtKeyboardMouse::commitPending() {
    if(!m_compose.pending) return true;
    const auto key=keys::ascii(m_compose.pending);
    if(!sys::BleHid::remoteConnected() || sys::BleHid::sendKey(key.code,key.modifiers)!=ESP_OK) {
        m_errorUntil=lv_tick_get()+800;cancelPending();return false;
    }
    cancelPending();return true;
}
void BtKeyboardMouse::keyCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));
    if(!sys::Power::isScreenOn()) return;
    auto action=static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
    if(action==10) {
        self->cancelPending();self->m_alphabet=static_cast<keys::Alphabet>((static_cast<unsigned>(self->m_alphabet)+1)%4);
        self->updateKeyLabels();return;
    }
    if(!sys::BleHid::remoteConnected()) { self->m_errorUntil=lv_tick_get()+800;self->cancelPending();return; }
    if(action==11) {
        if(self->m_compose.pending) self->cancelPending();
        else if(sys::BleHid::sendKey(0x2a)!=ESP_OK) self->m_errorUntil=lv_tick_get()+800;
        return;
    }
    if(action>9) return;
    const auto now=lv_tick_get();
    if((self->m_compose.key!=static_cast<int>(action) || self->m_compose.due(now)) && !self->commitPending()) return;
    self->m_compose.begin(self->m_alphabet,action,now);
    if(self->m_alphabet==keys::Alphabet::Numbers) self->commitPending();
    self->refreshPreview();
}
void BtKeyboardMouse::refreshPreview() {
    if(!m_preview) return;
    const char *modes[]={"abc","ABC","123","#+="};
    char text[40];const char value=m_compose.pending;
    if(!value) std::snprintf(text,sizeof(text),"%s",modes[static_cast<unsigned>(m_alphabet)]);
    else if(value==' ' || value=='\n' || value=='\t')
        std::snprintf(text,sizeof(text),"%s  [%s]",modes[static_cast<unsigned>(m_alphabet)],value==' '?"SPACE":value=='\n'?"ENTER":"TAB");
    else std::snprintf(text,sizeof(text),"%s   [ %c ]",modes[static_cast<unsigned>(m_alphabet)],value);
    ui::compact::setText(m_preview,text);
}
void BtKeyboardMouse::updateKeyLabels() {
    const char *letters[]={"0 _","1 .?!","2 abc","3 def","4 ghi","5 jkl","6 mno","7 pqrs","8 tuv","9 wxyz"};
    const char *caps[]={"0 _","1 .?!","2 ABC","3 DEF","4 GHI","5 JKL","6 MNO","7 PQRS","8 TUV","9 WXYZ"};
    const char *symbols[]={"0 _",".,?!","@#$_","+-=*/",":;\"'","()[]","{}<>","\\|~`","&%^","0..9"};
    for(unsigned i=0;i<10;++i) {
        char number[2]={static_cast<char>('0'+i),0};
        lv_label_set_text(m_digitLabels[i],m_alphabet==keys::Alphabet::Numbers?number:m_alphabet==keys::Alphabet::Symbols?symbols[i]:m_alphabet==keys::Alphabet::Upper?caps[i]:letters[i]);
    }
    lv_label_set_text(m_modeLabel,"aA#");refreshPreview();
}
void BtKeyboardMouse::clickRightCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));
    const auto code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESS_LOST) { self->m_clickLink=0;return; }
    if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_CLICKED) return;
    if(!self->m_visible || self->m_keyboard || !sys::Power::isScreenOn()) { self->m_clickLink=0;return; }
    if(code==LV_EVENT_PRESSED) { self->m_clickLink=sys::BleHid::mouseLinkGeneration();return; }
    const auto link=self->m_clickLink;self->m_clickLink=0;
    if(link && sys::BleHid::clickMouse(0x02,link)!=ESP_OK) self->m_errorUntil=lv_tick_get()+800;
}
void BtKeyboardMouse::speedCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));self->m_speedX2=!self->m_speedX2;self->updateSpeedLabel();
}
void BtKeyboardMouse::autoClickCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));self->m_autoClick=!self->m_autoClick;self->updateAutoLabel();self->restartAutoClick();
}
void BtKeyboardMouse::rateCb(lv_event_t *e) {
    auto self=static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));auto i=lv_dropdown_get_selected(lv_event_get_target_obj(e));
    self->m_autoRate=kRateOptions[i<4?i:0];if(self->m_autoClick) self->restartAutoClick();
}
void BtKeyboardMouse::updateSpeedLabel() {
    lv_label_set_text(m_speedLabel,m_speedX2?"x2":"x1");ui::compact::checked(lv_obj_get_parent(m_speedLabel),m_speedX2);
}
void BtKeyboardMouse::updateAutoLabel() {
    lv_label_set_text(m_autoLabel,m_autoClick?"C+":"C");ui::compact::checked(lv_obj_get_parent(m_autoLabel),m_autoClick);
}
void BtKeyboardMouse::restartAutoClick() {
    if(m_autoTimer) { lv_timer_delete(m_autoTimer);m_autoTimer=nullptr; }
    if(m_autoClick) m_autoTimer=lv_timer_create(autoClickTimerCb,1000/m_autoRate,this);
}
void BtKeyboardMouse::autoClickTimerCb(lv_timer_t *t) {
    auto self=static_cast<BtKeyboardMouse *>(lv_timer_get_user_data(t));
    // Preserve intentional mouse autoclick through ordinary screen-off, but
    // never click behind the keyboard or after leaving the app.
    if(self->m_visible && !self->m_keyboard && self->m_autoClick && sys::BleHid::remoteConnected() &&
       sys::BleHid::clickMouse(0x01)!=ESP_OK) self->m_errorUntil=lv_tick_get()+800;
}
void BtKeyboardMouse::statusTimerCb(lv_timer_t *t) { static_cast<BtKeyboardMouse *>(lv_timer_get_user_data(t))->refreshStatus(); }
void BtKeyboardMouse::refreshStatus() {
    if(sys::BleHid::takeKeyError()!=ESP_OK)m_errorUntil=lv_tick_get()+800;
    if(sys::BleHid::takeMouseError()!=ESP_OK) { m_errorUntil=lv_tick_get()+800;releasePointer(); }
    if(!m_visible || !sys::Power::isScreenOn()) return;
    const bool connected=sys::BleHid::remoteConnected();
    const bool linkChanged=connected!=m_wasConnected;
    if(!connected) { cancelPending();if(m_wasConnected) releasePointer(); }
    else if((m_pointerLink && m_pointerLink!=sys::BleHid::mouseLinkGeneration()) ||
            (m_clickLink && m_clickLink!=sys::BleHid::mouseLinkGeneration())) releasePointer();
    m_wasConnected=connected;
    if(m_keyboard && m_tab==2 && m_compose.due(lv_tick_get())) commitPending();
    // Composition and gesture cleanup retain the 50ms cadence. The status
    // rail needs only 4Hz and should not copy the whole bond list at 20Hz.
    const auto now=lv_tick_get();
    if(m_nextStatusMs && static_cast<int32_t>(now-m_nextStatusMs)<0 && !linkChanged) return;
    m_nextStatusMs=now+250;
    const auto state=sys::SystemService::bluetooth();
    if(m_errorUntil && static_cast<int32_t>(lv_tick_get()-m_errorUntil)>=0) m_errorUntil=0;
    const bool error=state.hasError() || (m_errorUntil && static_cast<int32_t>(m_errorUntil-lv_tick_get())>0);
    // Match Settings/system status: gray idle, amber transition, green linked.
    const bool pending=state.enabled && (state.state==sys::BleHidState::Starting || state.state==sys::BleHidState::Connecting);
    const int rail=error?3:state.peer.connected?2:pending?1:0;
    if(rail!=m_railState) {
        m_railState=rail;
        lv_obj_set_style_bg_color(m_status,error?ui::theme::danger():state.peer.connected?ui::theme::success():
            pending?ui::theme::warning():ui::theme::muted(),0);
    }
}
bool BtKeyboardMouse::emitMove(int32_t dx, int32_t dy, uint8_t buttons)
{
    // 1) 指针加速:按本次事件的瞬时速度(曼哈顿位移)非线性放大增益。
    //    慢速时接近基准增益(高精度);快速时增益增大(快速跨屏),并限制上限。
    const float speed = static_cast<float>(std::abs(dx) + std::abs(dy));
    float accel = 1.0f + kAccelFactor * speed;
    if (accel > kAccelCap) accel = kAccelCap;
    const float gain = kMoveGainBase * (m_speedX2 ? 2.0f : 1.0f) * accel;

    // 2) 小数累积:放大后的位移带小数,只取整数部分发送,余量留到下次,
    //    避免慢速滑动因整数截断而"移不动/发涩"。
    m_accX += static_cast<float>(dx) * gain;
    m_accY += static_cast<float>(dy) * gain;

    int32_t moveX = static_cast<int32_t>(m_accX);
    int32_t moveY = static_cast<int32_t>(m_accY);
    m_accX -= static_cast<float>(moveX);
    m_accY -= static_cast<float>(moveY);

    if (moveX == 0 && moveY == 0) return true;

    // Submit once per sample. The paced host worker keeps the complete delta
    // and splits it into HID reports without blocking the LVGL input callback.
    if(sys::BleHid::sendMouse(moveX, moveY, 0, buttons, m_pointerLink)!=ESP_OK) {
        m_errorUntil=lv_tick_get()+800;releasePointer();return false;
    }
    return true;
}

void BtKeyboardMouse::touchpadCb(lv_event_t *e)
{
    auto *self = static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING &&
       code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
    if(code==LV_EVENT_PRESS_LOST) { self->releasePointer(); return; }
    const auto link=sys::BleHid::mouseLinkGeneration();
    if(!self->m_visible || self->m_keyboard || !sys::Power::isScreenOn() || !link ||
       (code!=LV_EVENT_PRESSED && link!=self->m_pointerLink)) { self->releasePointer(); return; }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    const uint32_t now = lv_tick_get();

    if (code == LV_EVENT_PRESSED) {
        self->m_pointerLink=link;
        self->m_lastPoint = p;
        self->m_totalMove = 0;
        self->m_touching = true;
        self->m_pressMs = now;
        // 新手势开始,清空小数累积余量,避免上次残留造成起始跳动
        self->m_accX = 0.0f;
        self->m_accY = 0.0f;

        // 若上一次是有效轻点(tap),且本次按下落在拖拽窗口内 -> 进入拖拽待命。
        // 待命状态下,真正开始拖拽还需二次按下后有一定移动(kDragMoveStart),
        // 从而与"想再点一下"区分开,防止误触发拖拽。
        if (self->m_dragArmed &&
            (now - self->m_lastReleaseMs) <= kDragWindowMs) {
            // 保持待命,等待 PRESSING 判断是否真的开始拖动
            self->m_dragging = false;
        } else {
            self->m_dragArmed = false;
            self->m_dragging = false;
        }
    } else if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) && self->m_touching) {
        int32_t dx = p.x - self->m_lastPoint.x;
        int32_t dy = p.y - self->m_lastPoint.y;
        self->m_lastPoint = p;
        self->m_totalMove += std::abs(dx) + std::abs(dy);

        // 处于拖拽待命且移动量已达阈值 -> 正式进入按住左键拖拽
        if (self->m_dragArmed && !self->m_dragging &&
            self->m_totalMove >= kDragMoveStart) {
            self->m_dragging = true;
        }

        if (dx != 0 || dy != 0) {
            // 拖拽中带上左键按下位(0x01),否则普通移动(button=0);
            // 加速/小数累积/大位移拆分由 emitMove 统一处理。
            uint8_t buttons = self->m_dragging ? 0x01 : 0x00;
            if(!self->emitMove(dx, dy, buttons)) return;
        }
    }
    if (code == LV_EVENT_RELEASED && self->m_touching) {
        self->m_touching = false;
        self->m_pointerLink=0;
        const bool wasDragging = self->m_dragging;
        const uint32_t heldMs = now - self->m_pressMs;

        if (wasDragging) {
            // 拖拽结束:释放左键
            if(sys::BleHid::sendMouse(0, 0, 0, 0x00, link)!=ESP_OK) {
                self->m_errorUntil=now+800;self->releasePointer();return;
            }
            self->m_dragging = false;
            self->m_dragArmed = false;
        } else if (self->m_totalMove < kClickThreshold && heldMs <= kTapMaxMs) {
            // 一次有效轻点(位移小且时间短)
            if (self->m_dragArmed &&
                (now - self->m_lastReleaseMs) <= kDragWindowMs) {
                // 处于待命窗口内的第二次轻点却没有拖动 -> 视为双击,执行一次单击,
                // 并退出待命(不进入拖拽)
                if(sys::BleHid::clickMouse(0x01,link)!=ESP_OK) {
                    self->m_errorUntil=now+800;self->releasePointer();return;
                }
                self->m_dragArmed = false;
            } else {
                // 首次轻点:发送单击,并进入拖拽待命窗口
                if(sys::BleHid::clickMouse(0x01,link)!=ESP_OK) {
                    self->m_errorUntil=now+800;self->releasePointer();return;
                }
                self->m_dragArmed = true;
            }
        } else {
            // 位移大或按住时间长 -> 只是普通移动光标,退出待命
            self->m_dragArmed = false;
        }
        self->m_lastReleaseMs = now;
    }
}

void BtKeyboardMouse::wheelCb(lv_event_t *e)
{
    auto *self = static_cast<BtKeyboardMouse *>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING &&
       code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
    if(code==LV_EVENT_PRESS_LOST) { self->releasePointer(); return; }
    const auto link=sys::BleHid::mouseLinkGeneration();
    if(!self->m_visible || self->m_keyboard || !sys::Power::isScreenOn() || !link ||
       (code!=LV_EVENT_PRESSED && link!=self->m_pointerLink)) { self->releasePointer(); return; }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        self->m_pointerLink=link;
        self->m_wheelLastY = p.y;
        self->m_wheeling = true;
    } else if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) && self->m_wheeling) {
        // C++ signed division preserves the sub-step remainder for the next
        // sample and combines a fast swipe into one nonblocking submission.
        const int32_t steps=(p.y-self->m_wheelLastY)/kWheelStep;
        if(steps) {
            if(sys::BleHid::sendMouse(0,0,-steps,0,self->m_pointerLink)!=ESP_OK) {
                self->m_errorUntil=lv_tick_get()+800;self->releasePointer();return;
            }
            self->m_wheelLastY+=steps*kWheelStep;
        }
    }
    if (code == LV_EVENT_RELEASED) {
        self->m_wheeling = false;
        self->m_pointerLink=0;
    }
}


} // namespace apps
