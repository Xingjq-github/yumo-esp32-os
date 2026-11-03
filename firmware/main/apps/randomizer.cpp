#include "randomizer.hpp"
#include "random_layout.hpp"
#include "compact_ui.hpp"
#include "theme.hpp"
#include "text_input.hpp"
#include "event_bus.hpp"
#include "button_mgr.hpp"
#include "power_mgr.hpp"
#include "esp_random.h"
#include <cstdio>

namespace apps {
namespace {
namespace c=ui::compact;
namespace model=random_model;
namespace layout=random_layout;
lv_color_t gray() { return ui::theme::surface(); }
lv_color_t ink() { return ui::theme::muted(); }
lv_color_t teamInk(unsigned side) {
    const bool light=ui::theme::light();
    return lv_color_hex(side?(light?0xA92733:0xF1707A):(light?0x18558D:0x69B4FF));
}
void centered(lv_obj_t *label) { lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); }
void numberFont(lv_obj_t *label,const lv_font_t *font=&lv_font_montserrat_24) {
    lv_obj_set_style_text_font(label,font,0);
}
void place(lv_obj_t *obj,layout::Rect r) {
    lv_obj_set_size(obj,r.width,r.height);lv_obj_align(obj,LV_ALIGN_TOP_LEFT,r.x,r.y);
}
void cardStyle(lv_obj_t *obj) {
    lv_obj_set_style_radius(obj,layout::radius(ui::theme::current()),0);
    lv_obj_set_style_bg_color(obj,gray(),0);
    // An inset outline has no effect on child coordinates/content dimensions.
    lv_obj_set_style_outline_width(obj,ui::theme::tiles()?0:1,0);
    lv_obj_set_style_outline_pad(obj,-1,0);
    lv_obj_set_style_outline_color(obj,ui::theme::edge(),0);
}
}

void Randomizer::onCreate(lv_obj_t *parent)
{
    lv_obj_set_scrollable(parent,false);
    auto header=c::panel(parent,240,36);
    auto back=c::button(header,LV_SYMBOL_LEFT,[](lv_event_t *) {
        sys::EventBus::publish({sys::EventType::BackRequested});
    },nullptr,36,36);
    lv_obj_set_pos(back,0,0);
    m_modeSelect=c::dropdown(header,"数字\n骰子\n选项英文\n选项数字\n双色球\n硬币\n胜负\n石头剪刀布",203);
    lv_obj_set_pos(m_modeSelect,37,0); lv_dropdown_set_dir(m_modeSelect,LV_DIR_BOTTOM);
    lv_obj_add_event_cb(m_modeSelect,modeCb,LV_EVENT_VALUE_CHANGED,this);
    if(ui::theme::tiles() || ui::theme::terminal()) {
        lv_obj_set_style_bg_color(m_modeSelect,ui::theme::accent(),0);
        lv_obj_set_style_text_color(m_modeSelect,ui::theme::onAccent(),0);
    }
    // The shared system keyboard can shrink/scroll this viewport to a field.
    m_body=c::panel(parent,model::Width,model::BodyHeight,true);
    lv_obj_set_pos(m_body,0,37); lv_obj_set_scrollbar_mode(m_body,LV_SCROLLBAR_MODE_OFF);
    m_start=c::button(parent,"开始",[](lv_event_t *event) {
        static_cast<Randomizer *>(lv_event_get_user_data(event))->start();
    },this,240,40);
    lv_obj_set_pos(m_start,0,200);
    c::buttonBackground(m_start,ui::theme::accent());
    lv_obj_set_style_radius(m_start,layout::radius(ui::theme::current()),0);
    m_pkButton=c::button(parent,"PK",[](lv_event_t *event) {
        auto self=static_cast<Randomizer *>(lv_event_get_user_data(event));
        if(self->m_running) return;
        self->cancelAnimation();self->m_pkMode=!self->m_pkMode;self->buildMode();
    },this,60,40);
    lv_obj_set_pos(m_pkButton,180,200);
    m_timer=lv_timer_create([](lv_timer_t *timer) {
        static_cast<Randomizer *>(lv_timer_get_user_data(timer))->tick();
    },45,this);
    lv_timer_pause(m_timer);
    m_screenOffSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &) {
        cancelAnimation();
    });
    m_previousSub=sys::EventBus::subscribe(sys::EventType::PreviousPage,[this](const sys::Event &event) {
        if(m_keySession && event.i32==static_cast<int32_t>(m_keySession)) {
            if(m_pkMode) startPk(1);else start();
        }
    });
    m_nextSub=sys::EventBus::subscribe(sys::EventType::NextPage,[this](const sys::Event &event) {
        if(m_keySession && event.i32==static_cast<int32_t>(m_keySession)) {
            if(m_pkMode) startPk(2);else start();
        }
    });
    buildMode();
}

void Randomizer::buildMode()
{
    ui::TextInput::hide();
    cancelAnimation();
    if(m_lowField) {
        m_lowText=lv_textarea_get_text(m_lowField);
        m_highText=lv_textarea_get_text(m_highField);
    }
    m_mode=static_cast<Mode>(lv_dropdown_get_selected(m_modeSelect));
    const bool hands=m_mode==Mode::RockPaperScissors;
    if(!hands) m_pkMode=false;
    if(hands) lv_obj_set_hidden(m_pkButton,false);
    else lv_obj_set_hidden(m_pkButton,true);
    lv_obj_set_width(m_start,hands?179:240);
    c::buttonBackground(m_pkButton,m_pkMode?ui::theme::accent():gray());
    lv_obj_clean(m_body);
    m_pkResult=nullptr;m_pkScore[0]=m_pkScore[1]=nullptr;
    for(auto &side:m_pkCells) for(auto &cell:side) cell=nullptr;
    m_board=m_countSelect=m_lowField=m_highField=m_number=m_hint=nullptr;
    m_cells.fill(nullptr); m_ballLabels.fill(nullptr); m_cursor=-1;
    lv_obj_scroll_to_y(m_body,0,LV_ANIM_OFF);
    c::setText(lv_obj_get_child(m_start,0),"开始");
    if(m_pkMode) {
        buildPk();
    } else if(m_mode==Mode::Number) {
        const auto geometry=layout::number(ui::theme::current());
        auto low=c::label(m_body,"最小值",geometry.lowTitle.width); centered(low);place(low,geometry.lowTitle);
        auto high=c::label(m_body,"最大值",geometry.highTitle.width); centered(high);place(high,geometry.highTitle);
        m_lowField=c::field(m_body,"最小值",m_lowText.c_str(),7);
        m_highField=c::field(m_body,"最大值",m_highText.c_str(),7);
        place(m_lowField,geometry.low);place(m_highField,geometry.high);
        for(auto field:{m_lowField,m_highField}) {
            lv_textarea_set_accepted_chars(field,"0123456789-");
            lv_obj_set_style_pad_top(field,6,0); lv_obj_set_style_pad_bottom(field,6,0);
        }
        m_board=c::panel(m_body,geometry.result.width,geometry.result.height);place(m_board,geometry.result);
        cardStyle(m_board);
        if(ui::theme::focus()) lv_obj_set_style_bg_color(m_board,ui::theme::bg(),0);
        m_number=c::label(m_board,"--",geometry.result.width-8); numberFont(m_number,ui::theme::fontHuge());
        centered(m_number); lv_obj_center(m_number);
        m_hint=c::label(m_body,"范围 -999999～999999",240);
        place(m_hint,geometry.hint); centered(m_hint);
        lv_obj_set_style_text_color(m_hint,ui::theme::muted(),0);
    } else if(m_mode==Mode::Lottery) {
        m_board=c::panel(m_body,240,model::BodyHeight); buildLottery();
    } else {
        const bool adjustable=m_mode==Mode::Letters || m_mode==Mode::Choices;
        if(adjustable) {
            auto label=c::label(m_body,m_mode==Mode::Letters?"A 到":"选项数量",102);
            lv_obj_set_pos(label,0,7);
            const char *options=m_mode==Mode::Letters?"B\nC\nD\nE\nF\nG\nH":
                "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16";
            m_countSelect=c::dropdown(m_body,options,137); lv_obj_set_pos(m_countSelect,103,0);
            lv_dropdown_set_dir(m_countSelect,LV_DIR_BOTTOM);
            lv_dropdown_set_selected(m_countSelect,choiceCount()-2);
            lv_obj_add_event_cb(m_countSelect,countCb,LV_EVENT_VALUE_CHANGED,this);
        }
        const int top=adjustable?model::ParameterHeight+model::Gap:0;
        m_board=c::panel(m_body,240,model::BodyHeight-top); lv_obj_set_pos(m_board,0,top);
        buildChoices();
    }
}

unsigned Randomizer::choiceCount() const
{
    return m_mode==Mode::Coin?2:m_mode==Mode::Outcome?5:
        m_mode==Mode::RockPaperScissors?3:m_mode==Mode::Dice?6:m_mode==Mode::Letters?m_letters:m_choices;
}
void Randomizer::buildChoices()
{
    lv_obj_clean(m_board); m_cells.fill(nullptr); m_cursor=-1;
    c::setText(lv_obj_get_child(m_start,0),"开始");
    const bool dice=m_mode==Mode::Dice;
    const bool adjustable=m_mode==Mode::Letters || m_mode==Mode::Choices;
    const int height=model::BodyHeight-(adjustable?model::ParameterHeight+model::Gap:0);
    for(unsigned i=0;i<choiceCount();++i) {
        const auto rect=layout::cell(ui::theme::current(),choiceCount(),i,height);
        auto cell=c::panel(m_board,rect.width,rect.height);
        m_cells[i]=cell; lv_obj_set_pos(cell,rect.x,rect.y);
        cardStyle(cell);
        // Display-only choices: gestures still reach the App; no tiny buttons.
        lv_obj_set_clickable(cell,false);
        if(dice) {
            const int size=std::min(rect.width,rect.height), step=size/4, dot=std::max(6,size/9);
            for(unsigned p=0;p<9;++p) {
                if(!(model::DicePips[i]&(1u<<p))) continue;
                auto pip=c::panel(cell,dot,dot);
                lv_obj_set_style_radius(pip,LV_RADIUS_CIRCLE,0);
                lv_obj_set_style_bg_color(pip,ink(),0);
                lv_obj_set_pos(pip,rect.width/2+(int(p%3)-1)*step-dot/2,
                                   rect.height/2+(int(p/3)-1)*step-dot/2);
                lv_obj_set_clickable(pip,false);
            }
        } else {
            auto label=c::label(cell,"");
            numberFont(label,rect.height<32?&lv_font_montserrat_18:&lv_font_montserrat_24);
            if(m_mode==Mode::Coin || m_mode==Mode::Outcome || m_mode==Mode::RockPaperScissors) {
                static const char *coin[]={"正","反"};
                static const char *outcome[]={"胜","平","负","让平","让负"};
                static const char *hands[]={"石头","剪刀","布"};
                lv_obj_set_style_text_font(label,ui::theme::fontCjkFull(),0);
                lv_label_set_text(label,m_mode==Mode::Coin?coin[i]:
                    m_mode==Mode::Outcome?outcome[i]:hands[i]);
            } else if(m_mode==Mode::Letters) lv_label_set_text_fmt(label,"%c",int('A'+i));
            else lv_label_set_text_fmt(label,"%u",i+1);
            lv_obj_set_style_text_color(label,ink(),0); lv_obj_center(label);
        }
    }
}
void Randomizer::buildLottery()
{
    auto red=c::label(m_board,"红球",ui::theme::focus()?240:164); centered(red);
    auto blue=c::label(m_board,"蓝球",74); centered(blue);
    lv_obj_set_pos(blue,ui::theme::focus()?8:166,ui::theme::focus()?128:0);
    for(unsigned i=0;i<7;++i) {
        const auto r=layout::ball(ui::theme::current(),i);
        auto ball=c::panel(m_board,r.width,r.height); m_cells[i]=ball;place(ball,r);cardStyle(ball);
        if(ui::theme::warm())lv_obj_set_style_radius(ball,LV_RADIUS_CIRCLE,0);
        lv_obj_set_clickable(ball,false);
        m_ballLabels[i]=c::label(ball,"--"); numberFont(m_ballLabels[i]);
        lv_obj_set_style_text_color(m_ballLabels[i],ink(),0); lv_obj_center(m_ballLabels[i]);
    }
}

void Randomizer::showNumber(int value)
{
    char text[12];snprintf(text,sizeof(text),"%d",value);
    const int available=layout::number(ui::theme::current()).result.width-8;
    // The narrow tile still shows the complete signed six-digit range.
    for(auto font:{&lv_font_montserrat_48,&lv_font_montserrat_40,&lv_font_montserrat_32,&lv_font_montserrat_24}) {
        lv_point_t size;lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        if(size.x<=available) {numberFont(m_number,font);break;}
    }
    c::setText(m_number,text);
}

void Randomizer::lockControls(bool locked)
{
    for(auto control:{m_modeSelect,m_start,m_countSelect,m_lowField,m_highField})
        c::disabled(control,locked);
    c::disabled(m_pkButton,locked);
    // A second player must remain able to start while the first is spinning.
    if(m_pkMode) c::disabled(m_start,false);
}
void Randomizer::paintCell(unsigned index,bool selected,bool final)
{
    auto cell=m_cells[index];
    if(!cell) return;
    lv_obj_set_style_bg_color(cell,selected?(final?ui::theme::accent():ui::theme::edge()):gray(),0);
    for(unsigned i=0;i<lv_obj_get_child_count(cell);++i) {
        auto child=lv_obj_get_child(cell,i);
        const auto color=selected?(final?ui::theme::onAccent():ui::theme::fg()):ink();
        if(m_mode==Mode::Dice) lv_obj_set_style_bg_color(child,color,0);
        else lv_obj_set_style_text_color(child,color,0);
    }
}
void Randomizer::start()
{
    if(m_pkMode) {startPk(3);return;}
    if(!m_visible || m_running || !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
    ui::TextInput::hide();
    auto rng=[] { return esp_random(); };
    if(m_mode==Mode::Number) {
        if(!model::parseInteger(lv_textarea_get_text(m_lowField),m_low) ||
           !model::parseInteger(lv_textarea_get_text(m_highField),m_high)) {
            c::setText(m_hint,"请输入有效整数");
            lv_obj_set_style_text_color(m_hint,ui::theme::danger(),0); return;
        }
        if(m_low>m_high) {
            c::setText(m_hint,"最小值不能大于最大值");
            lv_obj_set_style_text_color(m_hint,ui::theme::danger(),0); return;
        }
        m_result=model::integer(m_low,m_high,rng); m_totalSteps=24;
        c::setText(m_hint,"抽取中"); lv_obj_set_style_text_color(m_hint,ui::theme::muted(),0);
        lv_obj_set_style_text_color(m_number,ui::theme::fg(),0);
    } else if(m_mode==Mode::Lottery) {
        m_lottery=model::lottery(rng); m_totalSteps=7;
        for(unsigned i=0;i<7;++i) {
            lv_label_set_text(m_ballLabels[i],"--");
            lv_obj_set_style_text_color(m_ballLabels[i],ink(),0);
            lv_obj_set_style_bg_color(m_cells[i],gray(),0);
        }
    } else {
        const unsigned count=choiceCount();
        for(unsigned i=0;i<count;++i) paintCell(i,false);
        m_cursor=-1;
        m_totalSteps=count*3+model::below(count,rng)+1;
    }
    m_step=0; m_running=true; lockControls(true);
    c::setText(lv_obj_get_child(m_start,0),"抽取中");
    lv_timer_set_period(m_timer,m_mode==Mode::Lottery?480:45);
    lv_timer_reset(m_timer); lv_timer_resume(m_timer);
    tick();
}
void Randomizer::tick()
{
    if(!m_running) return;
    if(!m_visible || !sys::Power::isScreenOn() || sys::Power::sleepPending()) {
        cancelAnimation(); return;
    }
    if(m_pkMode) {tickPk();return;}
    ++m_step;
    const bool final=m_step>=m_totalSteps;
    if(m_mode==Mode::Number) {
        auto rng=[] { return esp_random(); };
        showNumber(final?m_result:model::integer(m_low,m_high,rng));
        if(final) {
            lv_obj_set_style_text_color(m_number,ui::theme::accent(),0); c::setText(m_hint,"结果");
        } else lv_timer_set_period(m_timer,45+(m_step>14?(m_step-14)*20:0));
    } else if(m_mode==Mode::Lottery) {
        const unsigned index=m_step-1;
        const unsigned value=index<model::RedCount?m_lottery.red[index]:m_lottery.blue;
        lv_label_set_text_fmt(m_ballLabels[index],"%02u",value);
        lv_obj_set_style_text_color(m_ballLabels[index],lv_color_white(),0);
        lv_obj_set_style_bg_color(m_cells[index],lv_color_hex(index<model::RedCount?0xC52D44:0x2469B7),0);
    } else {
        if(m_cursor>=0) paintCell(static_cast<unsigned>(m_cursor),false);
        m_cursor=(m_step-1)%choiceCount(); paintCell(static_cast<unsigned>(m_cursor),true,final);
        const unsigned remaining=m_totalSteps-m_step;
        if(!final) lv_timer_set_period(m_timer,remaining<8?60+(8-remaining)*22:45);
    }
    if(final) finish();
}
void Randomizer::finish()
{
    m_running=false; if(m_timer) lv_timer_pause(m_timer);
    lockControls(false); c::setText(lv_obj_get_child(m_start,0),"再来一次");
}
void Randomizer::cancelAnimation()
{
    if(m_pkMode) {
        m_match.clearRound();m_pkCursor[0]=m_pkCursor[1]=-1;
        finish();paintPk();c::setText(lv_obj_get_child(m_start,0),"开始");return;
    }
    if(!m_running) return;
    finish();
    c::setText(lv_obj_get_child(m_start,0),"开始");
    if(m_mode==Mode::Number) {
        c::setText(m_number,"--"); c::setText(m_hint,"已停止");
    } else if(m_mode==Mode::Lottery) {
        for(unsigned i=0;i<7;++i) {
            lv_label_set_text(m_ballLabels[i],"--");
            lv_obj_set_style_bg_color(m_cells[i],gray(),0);
            lv_obj_set_style_text_color(m_ballLabels[i],ink(),0);
        }
    } else {
        for(unsigned i=0;i<choiceCount();++i) paintCell(i,false);
        m_cursor=-1;
    }
}
void Randomizer::buildPk()
{
    static const char *hands[]={"石头","剪刀","布"};
    m_match.clearRound();m_pkCursor[0]=m_pkCursor[1]=-1;
    for(unsigned side=0;side<2;++side) {
        m_pkScore[side]=c::label(m_body,"",100);
        lv_obj_set_pos(m_pkScore[side],side?140:0,0);centered(m_pkScore[side]);
        lv_obj_set_style_text_color(m_pkScore[side],teamInk(side),0);
        for(unsigned hand=0;hand<3;++hand) {
            auto cell=c::panel(m_body,78,44);m_pkCells[side][hand]=cell;
            lv_obj_set_pos(cell,side?162:0,26+int(hand)*46);
            cardStyle(cell);lv_obj_set_clickable(cell,false);
            auto label=c::label(cell,hands[hand]);lv_obj_center(label);
        }
    }
    auto vs=c::label(m_body,":",40);centered(vs);lv_obj_set_pos(vs,100,0);
    m_pkResult=c::label(m_body,"",80);centered(m_pkResult);
    lv_obj_set_pos(m_pkResult,80,86);
    paintPk();
}
void Randomizer::paintPk()
{
    for(unsigned side=0;side<2;++side) {
        if(m_pkScore[side]) {
            char score[32];std::snprintf(score,sizeof(score),"%s %u",side?"红":"蓝",m_match.score[side]);
            c::setText(m_pkScore[side],score);
        }
        for(unsigned hand=0;hand<3;++hand) {
            auto cell=m_pkCells[side][hand];if(!cell) continue;
            const bool selected=m_pkCursor[side]==int(hand);
            const bool done=m_match.state[side]==rps::Match::State::Done;
            const uint32_t color=side?(selected?(done?0xD94759:0x88414D):0x35242C):
                (selected?(done?0x247DCF:0x345D86):0x202E40);
            lv_obj_set_style_bg_color(cell,lv_color_hex(color),0);
            lv_obj_set_style_text_color(lv_obj_get_child(cell,0),selected?lv_color_white():
                lv_color_hex(side?0xF1A1AA:0x9FCDF8),0);
        }
    }
    if(m_pkResult) {
        c::setText(m_pkResult,m_match.result==0?"蓝胜":m_match.result==1?"红胜":m_match.result==2?"平局":"VS");
        lv_obj_set_style_text_color(m_pkResult,m_match.result==0?teamInk(0):
            m_match.result==1?teamInk(1):ui::theme::fg(),0);
    }
}
void Randomizer::startPk(unsigned sides)
{
    if(!m_pkMode || !m_visible || !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
    if(m_match.result>=0) {m_match.clearRound();m_pkCursor[0]=m_pkCursor[1]=-1;}
    const uint32_t now=lv_tick_get();auto rng=[] {return esp_random();};
    bool changed=false;
    for(unsigned side=0;side<2;++side) {
        if(!(sides&(1u<<side)) || !m_match.begin(side)) continue;
        m_pkStep[side]=0;m_pkTotal[side]=13+model::below(3,rng);
        m_pkCursor[side]=-1;m_pkDue[side]=now;changed=true;
    }
    if(!changed) return;
    m_running=true;lockControls(true);
    c::setText(lv_obj_get_child(m_start,0),"开始");
    lv_timer_set_period(m_timer,20);lv_timer_resume(m_timer);tickPk();
}
void Randomizer::tickPk()
{
    const uint32_t now=lv_tick_get();
    bool changed=false;
    for(unsigned side=0;side<2;++side) {
        if(m_match.state[side]!=rps::Match::State::Running || int32_t(now-m_pkDue[side])<0) continue;
        changed=true;
        m_pkCursor[side]=int(m_pkStep[side]++%3);
        const unsigned remaining=m_pkTotal[side]-m_pkStep[side];
        if(!remaining) m_match.complete(side,unsigned(m_pkCursor[side]));
        else m_pkDue[side]=now+(remaining<7?65+(7-remaining)*22:45);
    }
    // Poll at 20ms for independent players; redraw only when a reel advances.
    if(!changed)return;
    paintPk();
    if(!m_match.running()) {
        finish();c::setText(lv_obj_get_child(m_start,0),m_match.result>=0?"再来一次":"开始");
    }
}
void Randomizer::modeCb(lv_event_t *event)
{
    auto self=static_cast<Randomizer *>(lv_event_get_user_data(event));
    if(!self->m_running) self->buildMode();
}
void Randomizer::countCb(lv_event_t *event)
{
    auto self=static_cast<Randomizer *>(lv_event_get_user_data(event));
    if(self->m_running) return;
    const unsigned count=lv_dropdown_get_selected(self->m_countSelect)+2;
    if(self->m_mode==Mode::Letters) self->m_letters=std::min(8u,count);
    else self->m_choices=std::min(model::MaxChoices,count);
    self->buildChoices();
}
void Randomizer::onShow()
{
    m_keySession=sys::Buttons::claimPageKeys();
    m_visible=true;
}
void Randomizer::onHide()
{
    sys::Buttons::releasePageKeys(m_keySession); m_keySession=0;
    m_visible=false; cancelAnimation();
    if(m_timer) lv_timer_pause(m_timer);
    ui::TextInput::hide();
}
void Randomizer::onDestroy()
{
    onHide();
    sys::EventBus::unsubscribe(m_screenOffSub); m_screenOffSub=0;
    sys::EventBus::unsubscribe(m_previousSub); m_previousSub=0;
    sys::EventBus::unsubscribe(m_nextSub); m_nextSub=0;
    if(m_timer) { lv_timer_delete(m_timer); m_timer=nullptr; }
}
bool Randomizer::onBack()
{
    if(ui::TextInput::visible()) { ui::TextInput::hide(); return true; }
    return false;
}
} // namespace apps
