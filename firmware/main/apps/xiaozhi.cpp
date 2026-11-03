#include "xiaozhi.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include <algorithm>

namespace apps {
namespace {
namespace c=ui::compact;
namespace theme=ui::theme;
using State=core::xiaozhi::State;
bool active(State state) {
    return state==State::Connecting || state==State::Activating || state==State::Listening ||
           state==State::Thinking || state==State::Speaking;
}
const char *stateText(State state) {
    switch(state) {
    case State::Connecting:return "连接中";
    case State::Activating:return "等待绑定";
    case State::Ready:return "在线";
    case State::Listening:return "聆听中";
    case State::Thinking:return "思考中";
    case State::Speaking:return "播放中";
    case State::Failed:return "连接失败";
    case State::Stopped:return "已暂停";
    }
    return "";
}
const char *buttonText(State state) {
    switch(state) {
    case State::Connecting:return "连接中";
    case State::Activating:return "等待绑定";
    case State::Ready:return "开始说话";
    case State::Listening:return "结束说话";
    case State::Thinking:return "思考中";
    case State::Speaking:return "打断";
    case State::Failed:return "重试";
    case State::Stopped:return "连接";
    }
    return "";
}
const char *emptyText(State state) {
    switch(state) {
    case State::Connecting:return "正在连接小智";
    case State::Listening:return "我在听，请说";
    case State::Thinking:return "正在整理回答";
    case State::Speaking:return "正在回答";
    default:return "想聊点什么？";
    }
}
lv_color_t stateColor(State state) {
    switch(state) {
    case State::Failed:return theme::danger();
    case State::Ready:return theme::success();
    case State::Listening:case State::Speaking:return theme::accent();
    case State::Connecting:case State::Activating:case State::Thinking:return theme::warning();
    default:return theme::muted();
    }
}
}

void Xiaozhi::onCreate(lv_obj_t *parent) {
    lv_obj_set_scrollable(parent,false);
    lv_obj_set_style_bg_color(parent,theme::bg(),0);
    lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    auto back=c::button(parent,LV_SYMBOL_LEFT,[](lv_event_t *) {
        ui::WindowManager::instance().goHome();
    },nullptr,36,36);
    lv_obj_set_style_text_font(lv_obj_get_child(back,0),&lv_font_montserrat_24,0);
    lv_obj_set_style_bg_opa(back,LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_opa(back,LV_OPA_COVER,LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back,0,0);
    auto title=c::label(parent,"小智 AI",88);lv_obj_set_pos(title,40,9);
    lv_obj_set_style_text_font(title,theme::fontMenu(),0);
    auto statusBadge=c::panel(parent,98,24);lv_obj_set_pos(statusBadge,138,6);
    lv_obj_set_style_bg_color(statusBadge,theme::surface(),0);
    lv_obj_set_style_radius(statusBadge,theme::radius(),0);
    statusDot=c::panel(statusBadge,6,6);lv_obj_set_pos(statusDot,8,9);
    lv_obj_set_style_radius(statusDot,LV_RADIUS_CIRCLE,0);
    status=c::label(statusBadge,"",76);lv_obj_set_pos(status,18,3);
    lv_obj_set_style_text_font(status,theme::fontMenu(),0);
    lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);

    body=c::panel(parent,232,148,true);lv_obj_set_pos(body,4,40);
    lv_obj_set_style_pad_all(body,8,0);
    lv_obj_set_style_pad_row(body,8,0);lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(body,theme::radius(),0);
    lv_obj_set_style_bg_color(body,theme::surface(),0);
    lv_obj_set_style_bg_color(body,theme::muted(),LV_PART_SCROLLBAR);
    lv_obj_set_style_width(body,3,LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(body,bodyEvent,LV_EVENT_ALL,this);
    user=c::label(body,"",216);answer=c::label(body,"",216);
    for(auto label:{user,answer}) {
        lv_obj_set_style_text_font(label,theme::fontReading(),0);
        lv_obj_set_style_text_line_space(label,2,0);
        lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_clickable(label,false);
    }
    lv_obj_set_style_pad_hor(user,6,0);lv_obj_set_style_pad_ver(user,4,0);
    lv_obj_set_style_bg_color(user,lv_color_mix(theme::accent(),theme::surface(),24),0);
    lv_obj_set_style_bg_opa(user,LV_OPA_COVER,0);
    lv_obj_set_style_radius(user,theme::radius(),0);
    lv_obj_set_style_text_color(user,theme::muted(),0);

    // Decoration only occupies the otherwise empty conversation area.
    welcome=c::panel(parent,232,148);lv_obj_set_pos(welcome,4,40);
    lv_obj_set_style_bg_color(welcome,theme::surface(),0);
    lv_obj_set_style_radius(welcome,theme::radius(),0);
    auto halo=c::panel(welcome,72,72);lv_obj_set_pos(halo,80,20);
    lv_obj_set_style_radius(halo,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(halo,lv_color_mix(theme::accent(),theme::surface(),20),0);
    lv_obj_set_style_border_width(halo,1,0);
    lv_obj_set_style_border_color(halo,lv_color_mix(theme::accent(),theme::surface(),65),0);
    auto robot=c::label(halo,ui::icons::Xiaozhi);
    lv_obj_set_style_text_font(robot,ui::icons::font48(),0);lv_obj_center(robot);
    lv_obj_set_style_text_color(robot,theme::accent(),0);
    welcomeText=c::label(welcome,"",216);lv_obj_set_pos(welcomeText,8,106);
    lv_obj_set_style_text_font(welcomeText,theme::fontReading(),0);
    lv_obj_set_style_text_align(welcomeText,LV_TEXT_ALIGN_CENTER,0);

    binding=c::panel(parent,232,148);lv_obj_set_pos(binding,4,40);
    lv_obj_set_style_bg_color(binding,theme::surface(),0);
    lv_obj_set_style_radius(binding,theme::radius(),0);
    auto bindingTitle=c::label(binding,"设备绑定",216);lv_obj_set_pos(bindingTitle,8,12);
    lv_obj_set_style_text_font(bindingTitle,theme::fontReading(),0);
    lv_obj_set_style_text_color(bindingTitle,theme::muted(),0);
    lv_obj_set_style_text_align(bindingTitle,LV_TEXT_ALIGN_CENTER,0);
    bindingCode=c::label(binding,"",216);lv_obj_set_pos(bindingCode,8,39);
    lv_obj_set_style_text_font(bindingCode,&lv_font_montserrat_32,0);
    lv_obj_set_style_text_letter_space(bindingCode,3,0);
    lv_obj_set_style_text_align(bindingCode,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(bindingCode,theme::accent(),0);
    bindingHint=c::label(binding,"前往 xiaozhi.me\n添加设备，输入绑定码",224);
    lv_obj_set_pos(bindingHint,4,95);
    lv_obj_set_style_text_font(bindingHint,theme::fontMenu(),0);
    lv_obj_set_style_text_line_space(bindingHint,4,0);
    lv_obj_set_style_text_align(bindingHint,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(bindingHint,LV_LABEL_LONG_MODE_WRAP);

    talk=c::button(parent,"连接",click,this,232,44);lv_obj_set_pos(talk,4,192);
    c::buttonBackground(talk,theme::accent());talkText=lv_obj_get_child(talk,0);
    lv_obj_set_style_text_font(talkText,theme::fontReading(),0);
    lv_obj_align(talkText,LV_ALIGN_CENTER,0,-1);
    meter=lv_bar_create(talk);lv_obj_set_pos(meter,11,36);lv_obj_set_size(meter,208,3);
    lv_bar_set_range(meter,0,100);lv_obj_set_clickable(meter,false);
    lv_obj_set_style_bg_color(meter,lv_color_mix(theme::onAccent(),theme::accent(),40),0);
    lv_obj_set_style_bg_opa(meter,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(meter,theme::onAccent(),LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(meter,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_obj_set_style_radius(meter,2,0);lv_obj_set_style_radius(meter,2,LV_PART_INDICATOR);
    timer=lv_timer_create(tick,80,this);lv_timer_pause(timer);
    last.state=State::Stopped;render(last,true);
}

void Xiaozhi::onShow() {
    if(visible)return;
    visible=true;
    pageSession=sys::Buttons::claimPageKeys();
    backSession=sys::Buttons::claimBootBack();
    nextSub=sys::EventBus::subscribe(sys::EventType::NextPage,[this](const sys::Event &event) {
        if(visible && pageSession && event.i32==static_cast<int32_t>(pageSession))action();
    });
    offSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &) {suspend();});
    if(timer)lv_timer_resume(timer);
    connect();
}
void Xiaozhi::onHide() {
    visible=false;suspend();
    if(timer)lv_timer_pause(timer);
    sys::Buttons::releasePageKeys(pageSession);pageSession=0;
    sys::Buttons::releaseBootBack(backSession);backSession=0;
    if(nextSub)sys::EventBus::unsubscribe(nextSub);
    if(offSub)sys::EventBus::unsubscribe(offSub);
    nextSub=offSub=0;
}
void Xiaozhi::onDestroy() {
    onHide();job.reset();
    if(timer){lv_timer_delete(timer);timer=nullptr;}
    status=statusDot=meter=body=welcome=welcomeText=user=answer=binding=bindingCode=bindingHint=talk=talkText=nullptr;
}
void Xiaozhi::suspend() {
    stopped=true;haveSnapshot=false;touchingBody=false;
    if(job)core::xiaozhi::cancel(job);
}
uint32_t Xiaozhi::powerHints() const {
    return visible && !stopped && active(last.state)?
        sys::power::StayLit|sys::power::KeepSession:sys::power::None;
}
void Xiaozhi::connect() {
    if(!visible || !sys::Power::isScreenOn() || sys::Power::sleepPending())return;
    if(job && !job->done.load(std::memory_order_acquire))return;
    job.reset();stopped=false;haveSnapshot=false;
    job=core::xiaozhi::start();
    if(!job) {
        last={};last.state=State::Failed;last.notice="服务忙，请稍后重试";
        render(last,true);haveSnapshot=true;
    }
    poll();
}
void Xiaozhi::action() {
    if(!visible || !sys::Power::isScreenOn() || sys::Power::sleepPending())return;
    if(ui::WindowManager::instance().inputRoot()!=root())return;
    sys::Power::activity();
    if(!job || job->done.load(std::memory_order_acquire)) {connect();return;}
    if(!stopped)core::xiaozhi::toggleTalk(job);
    poll();
}
void Xiaozhi::click(lv_event_t *event) {
    auto self=static_cast<Xiaozhi *>(lv_event_get_user_data(event));
    if(!lv_obj_has_state(lv_event_get_target_obj(event),LV_STATE_DISABLED))self->action();
}
void Xiaozhi::bodyEvent(lv_event_t *event) {
    auto self=static_cast<Xiaozhi *>(lv_event_get_user_data(event));
    switch(lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:self->touchingBody=true;break;
    case LV_EVENT_RELEASED:case LV_EVENT_PRESS_LOST:self->touchingBody=false;break;
    default:break;
    }
}
void Xiaozhi::tick(lv_timer_t *timer) {static_cast<Xiaozhi *>(lv_timer_get_user_data(timer))->poll();}
void Xiaozhi::poll() {
    if(!visible)return;
    if(sys::Buttons::takeBootBack(backSession)) {ui::WindowManager::instance().goHome();return;}
    if(!sys::Power::isScreenOn() || sys::Power::sleepPending()) {
        if(!stopped)suspend();
        return;
    }
    if(!job)return;
    auto current=job->snapshot();
    const bool done=job->done.load(std::memory_order_acquire);
    if(stopped) {current.state=State::Stopped;current.notice="点击连接继续";current.activationCode.clear();}
    if(!haveSnapshot || current.revision!=last.revision || current.state!=last.state ||
       current.level!=last.level || done!=finished)render(current,done);
}
void Xiaozhi::render(const core::xiaozhi::Snapshot &current,bool done) {
    // Audio levels also publish a revision. Do not re-style or lay out text at
    // microphone frequency when only that level has changed.
    const bool stateChanged=!haveSnapshot || current.state!=last.state;
    const bool changed=stateChanged || done!=finished || current.notice!=last.notice ||
        current.userText!=last.userText || current.assistantText!=last.assistantText ||
        current.activationCode!=last.activationCode;
    const bool activating=current.state==State::Activating && !current.activationCode.empty();
    if(changed) {
        if(stateChanged) {
            c::setText(status,stateText(current.state));
            lv_obj_set_style_text_color(status,stateColor(current.state),0);
            lv_obj_set_style_bg_color(statusDot,stateColor(current.state),0);
            lv_obj_set_hidden(meter,current.state!=State::Listening);
        }
        const bool failed=current.state==State::Failed || current.state==State::Stopped;
        const bool empty=!activating && !failed && current.userText.empty() &&
            current.assistantText.empty() && current.notice.empty();
        const bool wasHidden=lv_obj_is_hidden(body);
        lv_obj_set_hidden(body,activating || empty);lv_obj_set_hidden(binding,!activating);
        lv_obj_set_hidden(welcome,!empty);
        if(empty)c::setText(welcomeText,emptyText(current.state));
        if(activating)c::setText(bindingCode,current.activationCode.c_str());
        else if(!empty) {
            const std::string userText=current.userText.empty() || failed?"":"我 · "+current.userText;
            const bool reply=!current.assistantText.empty() && !failed;
            const std::string text=reply?current.assistantText:
                !current.notice.empty()?current.notice:emptyText(current.state);
            const bool userChanged=userText!=lv_label_get_text(user);
            const bool answerChanged=text!=lv_label_get_text(answer);
            const bool reading=touchingBody || lv_obj_is_scrolling(body);
            const bool follow=!reading && lv_obj_get_scroll_bottom(body)<=12;
            c::setText(user,userText.c_str());lv_obj_set_hidden(user,userText.empty());
            c::setText(answer,text.c_str());
            if(userChanged || answerChanged || wasHidden) {
                lv_obj_update_layout(body);
                const bool firstReply=reply && (last.assistantText.empty() || !haveSnapshot);
                if(firstReply && !reading) {
                    // A long question must not bury the beginning of the reply.
                    const int start=lv_obj_get_y(answer);
                    const bool buried=start>=lv_obj_get_content_height(body)-theme::fontReading()->line_height;
                    lv_obj_scroll_to_y(body,buried?start:0,LV_ANIM_OFF);
                } else if(wasHidden || failed || (userChanged && !reading)) {
                    lv_obj_scroll_to_y(body,0,LV_ANIM_OFF);
                } else if(reply && answerChanged && follow) {
                    lv_obj_scroll_to_y(body,lv_obj_get_scroll_y(body)+lv_obj_get_scroll_bottom(body),LV_ANIM_OFF);
                }
            }
        }
        c::setText(talkText,buttonText(current.state));
        const bool canTalk=current.state==State::Ready || current.state==State::Listening || current.state==State::Speaking;
        const bool canRetry=(current.state==State::Failed || current.state==State::Stopped) && done;
        c::disabled(talk,!(canTalk || canRetry) || (stopped && !done));
    }
    const unsigned level=current.state==State::Listening?std::min(current.level,100u):0;
    if(!haveSnapshot || level!=(last.state==State::Listening?std::min(last.level,100u):0))
        lv_bar_set_value(meter,level,LV_ANIM_OFF);
    last=current;finished=done;haveSnapshot=true;
}
}
