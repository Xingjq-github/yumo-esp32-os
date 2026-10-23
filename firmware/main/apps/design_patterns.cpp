#include "design_patterns.hpp"
#include "ui/compact_ui.hpp"
#include "ui/code_view.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
LV_FONT_DECLARE(lv_font_code_13);
LV_FONT_DECLARE(lv_font_menu_14);
namespace apps {
namespace {
namespace c=ui::compact;namespace p=core::patterns;namespace play=p::play;
const char *Groups[]={"基础训练场","创建型工厂","结构型装配场","行为型控制室"};
const char *Stages[]={"跟着做","改造任务","独立挑战"};
lv_color_t ink(){return lv_color_hex(0xE8F1FF);}lv_color_t muted(){return lv_color_hex(0x9BADCA);}
lv_color_t bg(){return lv_color_hex(0x0C1525);}lv_color_t surface(){return lv_color_hex(0x182A42);}
lv_color_t cyan(){return lv_color_hex(0x63DDD0);}lv_color_t amber(){return lv_color_hex(0xFFD080);}
lv_color_t red(){return lv_color_hex(0xFF9A9A);}lv_color_t edge(){return lv_color_hex(0x324D68);}
unsigned countBits(unsigned x){return (x&1)+((x>>1)&1)+((x>>2)&1);}
}
lv_obj_t *DesignPatterns::panel(lv_obj_t *where,int x,int y,int w,int h){
    auto obj=lv_obj_create(where);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,bg(),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(obj,ink(),0);lv_obj_set_scrollable(obj,false);
    lv_obj_set_gesture_bubble(obj,false);return obj;
}
lv_obj_t *DesignPatterns::label(lv_obj_t *where,const std::string &text,int x,int y,int w,bool wrap){
    auto l=lv_label_create(where);lv_obj_remove_style_all(l);lv_obj_set_style_text_font(l,&lv_font_menu_14,0);
    lv_obj_set_style_text_color(l,ink(),0);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);
    lv_label_set_long_mode(l,wrap?LV_LABEL_LONG_MODE_WRAP:LV_LABEL_LONG_MODE_DOTS);lv_label_set_text(l,text.c_str());return l;
}
lv_obj_t *DesignPatterns::button(lv_obj_t *where,const char *text,Action action,unsigned value,int x,int y,int w,int h,bool strong){
    auto b=lv_button_create(where);lv_obj_remove_style_all(b);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_radius(b,8,0);lv_obj_set_style_bg_color(b,strong?cyan():surface(),0);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,strong?cyan():edge(),0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x427775),LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b,amber(),LV_STATE_PRESSED);lv_obj_set_style_opa(b,LV_OPA_40,LV_STATE_DISABLED);
    lv_obj_set_scrollable(b,false);lv_obj_set_gesture_bubble(b,false);
    lv_obj_set_user_data(b,reinterpret_cast<void *>(uintptr_t(unsigned(action)|(value<<8))));lv_obj_add_event_cb(b,click,LV_EVENT_CLICKED,this);
    if(text&&*text){auto t=label(b,text,4,0,w-8);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);
        if(static_cast<unsigned char>(*text)==0xEF)lv_obj_set_style_text_font(t,&lv_font_montserrat_18,0);
        lv_obj_set_style_text_color(t,strong?bg():ink(),0);lv_obj_center(t);}
    if(!timer&&action!=Action::Back)c::disabled(b,true);
    return b;
}
void DesignPatterns::onCreate(lv_obj_t *root){
    parent=root;lv_obj_set_scrollable(root,false);lv_obj_set_gesture_bubble(root,false);
    lv_obj_set_style_bg_color(root,bg(),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    header=panel(root,0,0,240,36);body=panel(root,0,36,240,164);footer=panel(root,0,200,240,40);
    timer=lv_timer_create([](lv_timer_t *t){static_cast<DesignPatterns *>(lv_timer_get_user_data(t))->poll();},40,this);
    if(timer)lv_timer_pause(timer);
    prevSub=sys::EventBus::subscribe(sys::EventType::PreviousPage,[this](const sys::Event &e){if(keys&&e.i32==int32_t(keys)&&!hidden){if(page==Page::Workshop)undo();else if(!onBack())ui::WindowManager::instance().goHome();}});
    nextSub=sys::EventBus::subscribe(sys::EventType::NextPage,[this](const sys::Event &e){if(keys&&e.i32==int32_t(keys)&&!hidden)primary();});
    screenSub=sys::EventBus::subscribe(sys::EventType::ScreenOff,[this](const sys::Event &){if(page==Page::Running){paused=true;draw();}flush();});
}
void DesignPatterns::onShow(){hidden=false;keys=sys::Buttons::claimPageKeys();if(timer){lv_timer_resume(timer);loadProgress();}else{message="内存不足，请返回后重试";draw();}}
void DesignPatterns::onHide(){
    if(hidden)return;
    hidden=true;if(job){job->cancelled.store(true);job.reset();}flush();saving.reset();
    sys::Buttons::releasePageKeys(keys);keys=0;if(timer)lv_timer_pause(timer);
    lesson.reset();history={};historySize=0;std::string().swap(message);game={};
    clearContent(); // Release scene/scroll animations before the App root is deleted.
}
void DesignPatterns::onDestroy(){onHide();if(timer)lv_timer_delete(timer);timer=nullptr;
    sys::EventBus::unsubscribe(prevSub);sys::EventBus::unsubscribe(nextSub);sys::EventBus::unsubscribe(screenSub);prevSub=nextSub=screenSub=0;
    parent=header=body=footer=board=status=nullptr;
}
uint32_t DesignPatterns::powerHints()const{return page==Page::Info||page==Page::Code||page==Page::Help?sys::power::StayLit|sys::power::LowLoad|sys::power::Dim:sys::power::None;}
void DesignPatterns::onPowerSaving(bool low){if(timer)lv_timer_set_period(timer,low?500:40);}
void DesignPatterns::markDirty(){if(!atlas){progress.lesson=index;progress.stage=stage;progress.design=0;progress.option=0;}dirty=true;changed=lv_tick_get();access=core::sd::requestAccess();}
void DesignPatterns::flush(){
    if(!dirty)return;
    if(!generation){message="无SD卡，本次进度暂存";dirty=false;return;}
    auto next=p::save(progress,generation,access);if(next){saving=std::move(next);dirty=false;}else{changed=lv_tick_get();message="记录未保存，可在帮助中重试";}
}
void DesignPatterns::loadProgress(){
    if(job)return;
    flush();page=Page::Loading;message="正在读取工坊进度";job=p::open();
    if(!job){page=Page::Home;ready=true;message="存储忙，可先临时游戏";}draw();
}
void DesignPatterns::request(unsigned wanted,unsigned phase,bool){
    if(wanted>=p::LessonCount||phase>=3||job)return;
    index=wanted;stage=phase;lesson.reset();passed=paused=false;page=Page::Loading;message="正在打开工作台";
    job=p::read(index,generation);if(!job){page=Page::Home;message="内存或存储忙，请重试";}draw();
}
void DesignPatterns::stagePage(){game.reset(index,stage);historySize=0;hints=stage==0;if(!hints)game.say("操作部件完成目标，需要时点提示");passed=paused=false;frame=0;page=Page::Brief;guide();}
void DesignPatterns::guide(){highlighted=hints?game.hint():-1;}
void DesignPatterns::poll(){
    if(hidden)return;
    if(saving&&saving->done.load(std::memory_order_acquire)){
        if(saving->error&&saving->error!=ECANCELED){message="记录未保存，请检查SD卡";generation=0;}else if(!saving->error)message="进度已保存";
        saving.reset();if(page==Page::Home)draw();
    }
    if(dirty&&lv_tick_elaps(changed)>=1600&&!saving)flush();
    if(job&&job->done.load(std::memory_order_acquire)){
        auto finished=std::move(job);
        if(finished->operation==p::Operation::Open){
            ready=true;if(!finished->error){generation=finished->generation;auto loaded=finished->progress;
                if(keepSession){for(unsigned i=0;i<p::LessonCount;++i)loaded.complete[i]|=progress.complete[i];if(progress.started){loaded.lesson=progress.lesson;loaded.stage=progress.stage;loaded.started=true;}}
                progress=loaded;if(keepSession){dirty=true;changed=lv_tick_get();access=core::sd::requestAccess();}keepSession=false;
                message=finished->damaged?"旧记录无效，可重新闯关":"点击开始，修好第一座工作站";
            }else{generation=0;message="无SD卡，也可以临时游戏";}page=Page::Home;
        }else if(!finished->error&&finished->lesson){lesson=std::move(finished->lesson);message.clear();if(atlas){page=Page::Info;infoReturn=Page::List;}else{progress.started=true;markDirty();stagePage();}}
        else{page=Page::Home;message="关卡打开失败，请重试";}
        draw();return;
    }
    if(page==Page::Running&&!paused&&sys::Power::isScreenOn()){
        const auto elapsed=lv_tick_elaps(stepAt);const unsigned duration=std::max(1u,game.route.size-1)*180;
        if(elapsed>=duration){if(game.won)finish();else{page=Page::Workshop;guide();draw();}}
        else if(lv_tick_elaps(frameAt)>=40){frameAt=lv_tick_get();frame=elapsed; if(board)lv_obj_invalidate(board);}
    }
}
void DesignPatterns::clearContent(){
    board=status=nullptr;if(header)lv_obj_clean(header);if(body)lv_obj_clean(body);if(footer)lv_obj_clean(footer);
    if(body){lv_obj_scroll_to(body,0,0,LV_ANIM_OFF);lv_obj_set_scrollable(body,false);lv_obj_set_scroll_dir(body,LV_DIR_VER);}
}
// Lightweight circuit/parcel animation. Only this 240x108 area is invalidated;
// no framebuffer, asset decoding, task, or per-frame object allocation.
void DesignPatterns::paint(lv_event_t *e){
    auto self=static_cast<DesignPatterns *>(lv_event_get_user_data(e));auto layer=lv_event_get_layer(e);
    lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);
    auto rect=[&](int x,int y,int w,int h,lv_color_t color,int radius=0){lv_draw_rect_dsc_t r;lv_draw_rect_dsc_init(&r);r.bg_color=color;r.radius=radius;lv_area_t box={a.x1+x,a.y1+y,a.x1+x+w-1,a.y1+y+h-1};lv_draw_rect(layer,&r,&box);};
    auto line=[&](int x,int y,int xx,int yy,lv_color_t color,int width){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=color;d.width=width;d.p1={lv_value_precise_t(a.x1+x),lv_value_precise_t(a.y1+y)};d.p2={lv_value_precise_t(a.x1+xx),lv_value_precise_t(a.y1+yy)};lv_draw_line(layer,&d);};
    for(int y=6;y<108;y+=12)for(int x=6;x<240;x+=12)rect(x,y,1,1,edge());
    constexpr int xs[]={54,186,54,186},ys[]={24,24,84,84};
    for(unsigned i=0;i<4;++i){line(xs[i],ys[i],120,ys[i],edge(),2);line(120,ys[i],120,54,edge(),2);}
    // Small workshop prop, selected by the lesson. No image assets are loaded.
    const auto &v=self->game.v;
    switch(self->index){
    case 2: // Vehicle and wheels.
        rect(106,50,28,11,cyan(),3);rect(112,43,15,9,cyan(),2);rect(115,45,9,5,bg());
        rect(110,59,6,6,amber(),3);rect(125,59,6,6,amber(),3);break;
    case 6:case 5: // Finished package, otherwise open container.
        rect(107,47,26,18,amber(),2);rect(107,46,26,3,cyan());rect(118,47,4,18,bg());
        if(!v[3]){line(107,47,112,40,cyan(),3);line(133,47,128,40,cyan(),3);}break;
    case 11: // Coffee and distinct cream/sugar layers.
        rect(108,43,21,21,ink(),3);rect(129,48,5,10,ink(),2);rect(129,50,3,6,bg());
        rect(111,47,15,13,lv_color_hex(0xAD795A));
        for(int j=1;j<=v[0];++j)rect(111,60-j*4,15,3,v[j]==1?ink():amber());
        break;
    case 16: // The receiver's actual light state.
        rect(111,40,18,19,v[2]?amber():edge(),8);rect(115,59,10,4,ink());rect(117,63,6,3,ink());
        if(v[2]){line(105,43,108,45,amber(),2);line(132,45,135,43,amber(),2);}break;
    case 21:case 8:case 14: // Instrument display.
        rect(106,42,28,21,cyan(),3);rect(109,45,22,14,bg(),1);rect(117,63,6,3,ink());
        for(int j=0;j<3;++j)rect(112+j*6,55-j*3,3,3+j*3,amber());
        break;
    case 4:case 9: // Envelope.
        rect(106,43,28,20,ink(),2);line(108,45,120,54,edge(),2);line(120,54,132,45,edge(),2);break;
    case 10:case 7:case 20: // Folder / snapshot.
        rect(106,45,12,5,amber(),1);rect(106,49,28,16,amber(),2);rect(111,52,18,3,bg());break;
    case 22: // Vending machine, product disappears after dispense.
        rect(108,39,24,28,cyan(),2);rect(111,42,13,15,bg());rect(127,43,2,6,amber());rect(113,60,14,4,bg());
        if(v[0]!=2)rect(115,47,5,8,amber(),1);
        break;
    default:
        rect(111,43,18,18,cyan(),3);rect(114,48,3,3,bg());rect(123,48,3,3,bg());rect(117,56,6,2,bg());
        rect(118,37,4,6,amber());rect(108,48,3,8,cyan());rect(129,48,3,8,cyan());break;
    }
    if(self->page==Page::Running&&self->game.route.size){
        const auto &r=self->game.route;unsigned segment=std::min(self->frame/180,r.size-1);unsigned to=std::min(segment+1,r.size-1);
        unsigned fromNode=r.nodes[segment],toNode=r.nodes[to];float t=float(self->frame%180)/180.f;
        int x=int(xs[fromNode]+(xs[toNode]-xs[fromNode])*t),y=int(ys[fromNode]+(ys[toNode]-ys[fromNode])*t);
        line(xs[fromNode],ys[fromNode],xs[toNode],ys[toNode],self->game.error?red():cyan(),2);
        rect(x-4,y-4,9,9,amber(),2);
    }
}
void DesignPatterns::scene(){
    const auto &m=play::Missions[index];
    auto goal=label(body,m.goals[stage],8,0,224,true);lv_obj_set_height(goal,36);
    for(unsigned i=0;i<3;++i){auto mark=panel(body,8+int(i)*76,33,72,2);lv_obj_set_style_bg_color(mark,i<=stage?cyan():edge(),0);}
    board=panel(body,0,36,240,108);lv_obj_add_event_cb(board,paint,LV_EVENT_DRAW_MAIN,this);
    for(unsigned i=0;i<4;++i){int x=(i&1)?136:4,y=i<2?1:61;
        auto node=button(board,"",Action::Node,i,x,y,100,44);const bool active=game.editable(i);
        if(!active){lv_obj_set_clickable(node,false);lv_obj_set_style_bg_color(node,lv_color_hex(0x142239),0);}
        lv_obj_set_style_border_color(node,highlighted==int(i)?amber():active?cyan():edge(),0);lv_obj_set_style_border_width(node,highlighted==int(i)?2:1,0);
        auto title=label(node,m.nodes[i],5,3,90);lv_obj_set_style_text_color(title,active?ink():muted(),0);
        char text[64];game.value(i,text,sizeof(text));auto val=label(node,text,5,23,90);lv_obj_set_style_text_color(val,cyan(),0);
        if(page==Page::Running)lv_obj_set_clickable(node,false);
    }
    const char *prompt=game.message;
    std::string hintText;
    if(page==Page::Workshop&&hints){if(highlighted>=0&&highlighted<4)hintText="下一步：点“"+std::string(m.nodes[highlighted])+"”";else if(highlighted==4)hintText="下一步：点下方“"+std::string(m.fire)+"”";else hintText="试着撤销，或点重来恢复工作台";prompt=hintText.c_str();}
    status=label(body,prompt,6,146,228);lv_obj_set_style_text_color(status,game.error?red():muted(),0);
    lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    if(page==Page::Running){
        button(footer,"返回",Action::Back,0,4,1,64,38);
        auto b=button(footer,paused?"继续动画":"运行中",Action::Resume,0,74,1,162,38,true);if(!paused)c::disabled(b,true);
    }else{
        button(footer,"撤销",Action::Undo,0,4,1,48,38);
        button(footer,"提示",Action::Hint,0,56,1,48,38);
        auto fire=button(footer,m.fire,Action::Run,0,108,1,128,38,true);
        if(highlighted==4){lv_obj_set_style_border_width(fire,2,0);lv_obj_set_style_border_color(fire,amber(),0);}
    }
}
void DesignPatterns::draw(){
    if(!parent||hidden)return;
    clearContent();
    button(header,LV_SYMBOL_LEFT,Action::Back,0,4,0,36,36);
    const char *title="对象工坊";
    if(page==Page::Groups)title=atlas?"模式图鉴":"选择区域";
    else if(page==Page::List)title=Groups[group];else if(page==Page::Help)title="工坊指南";
    else if(page==Page::Code)title="Java 代码";
    else if(lesson&&page!=Page::Home&&page!=Page::Loading)title=lesson->title.c_str();
    label(header,title,46,9,148);
    if(page==Page::Workshop||page==Page::Running)button(header,LV_SYMBOL_LIST,Action::Info,0,200,0,36,36);
    else if(lesson&&(page==Page::Brief||page==Page::Result))label(header,std::to_string(stage+1)+"/3",204,9,34);
    if(page==Page::Home){
        unsigned stars=0;for(auto c:progress.complete)stars+=countBits(c);
        auto hero=panel(body,8,0,224,57);lv_obj_set_style_bg_color(hero,surface(),0);lv_obj_set_style_radius(hero,10,0);
        label(hero,"修好工作站，让对象协作",10,7,204);auto info=label(hero,!generation&&!message.empty()?message:"动手搭建 / 运行 / 完成交付",10,31,204);lv_obj_set_style_text_color(info,cyan(),0);lv_label_set_long_mode(info,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        auto start=button(body,progress.started?"继续冒险":"开始冒险",Action::Continue,0,8,65,224,46,true);c::disabled(start,!ready||!timer);
        button(body,"关卡地图",Action::Groups,0,8,117,108,40);button(body,"模式图鉴",Action::Atlas,0,124,117,108,40);
        label(footer,"完成 "+std::to_string(stars)+" / 78",8,11,166);button(footer,LV_SYMBOL_SETTINGS,Action::Help,0,196,1,40,38);return;
    }
    if(page==Page::Loading){label(body,message,12,50,216,true);label(footer,"可返回，文件操作会安全收尾",8,10,224);return;}
    if(page==Page::Groups){for(unsigned i=0;i<4;++i)button(body,Groups[i],Action::Category,i,8,int(i*41),224,39);label(footer,"每站三项任务，可自由重玩",8,11,224);return;}
    if(page==Page::List){
        std::array<unsigned,p::LessonCount> items{};unsigned size=0;for(unsigned i=0;i<p::LessonCount;++i)if(p::Catalogs[i].group==group)items[size++]=i;
        for(unsigned row=0;row<4&&listOffset+row<size;++row){auto id=items[listOffset+row];auto b=button(body,"",Action::Entry,id,8,int(row*41),224,39);
            auto number=label(b,std::to_string(id+1),7,10,25);lv_obj_set_style_text_color(number,cyan(),0);label(b,p::Catalogs[id].title,35,10,150);
            label(b,std::to_string(countBits(progress.complete[id]))+"/3",189,10,30);}
        auto prev=button(footer,LV_SYMBOL_LEFT,Action::Prev,0,8,1,52,38);c::disabled(prev,!listOffset);
        label(footer,std::to_string(listOffset/4+1)+" / "+std::to_string((size+3)/4),93,11,65);
        auto next=button(footer,LV_SYMBOL_RIGHT,Action::Next,0,180,1,52,38);c::disabled(next,listOffset+4>=size);return;
    }
    if(page==Page::Help){
        lv_obj_set_scrollable(body,true);
        label(body,"1. 看顶部目标，点部件装配或接线。\n2. 点底部运行，观察请求流向。\n3. 达成目标，领取完成标记。\n\n首项任务有描边引导，后两项可点提示。菜单可重来、看讲解和Java。\nBOOT撤销，KEY执行；关屏暂停，点继续恢复。\n\n退出保存任务进度，重进从当前任务初始状态开始。无SD可临时玩，不保存进度。\n\n模拟对象行为，不运行Java；撤销仅是游戏工具。",8,2,224,true);
        button(footer,"模式图鉴",Action::Atlas,0,8,1,108,38);button(footer,"重读SD",Action::Reload,0,124,1,108,38);return;
    }
    if(!lesson)return;
    if(page==Page::Brief){
        auto badge=label(body,Stages[stage],8,0,224);lv_obj_set_style_text_color(badge,cyan(),0);
        auto card=panel(body,8,24,224,68);lv_obj_set_style_bg_color(card,surface(),0);lv_obj_set_style_radius(card,10,0);
        label(card,"任务："+std::string(play::Missions[index].scene),9,6,206);label(card,play::Missions[index].goals[stage],9,29,206,true);
        label(body,std::string(game.setup())+(stage==0?"\n金色描边提示下一步。":"\n需要时可点击提示。"),8,98,224,true);
        button(footer,"开始任务",Action::Start,0,8,1,224,38,true);return;
    }
    if(page==Page::Workshop||page==Page::Running){scene();return;}
    if(page==Page::Info){
        lv_obj_set_scrollable(body,true);
        std::string text="工作站："+std::string(play::Missions[index].scene)+"\n\n"+play::Missions[index].lesson+"\n\n模式意图\n"+lesson->intent+"\n\nJava 角色\n";
        for(auto &r:lesson->roles)text+=r+"\n";
        text+="\n扩展边界\n"+lesson->boundary+"\n\n适用与代价\n"+lesson->caution;
        text+="\n\n初始结构在任务页说明；Java的option=0/1/2对应三项任务。撤销是游戏操作，不代表模式能力。";
        label(body,text,8,2,224,true);
        button(footer,atlas?"开始玩":infoReturn==Page::Result?"返回交付":"回工作台",Action::Practice,0,4,1,100,38,true);
        button(footer,"Java",Action::Code,0,108,1,60,38);button(footer,"重来",Action::Reset,0,172,1,64,38);return;
    }
    if(page==Page::Code){
        const auto source=ui::code::unindent(lesson->code,ui::code::commonIndent(lesson->code));
        auto view=ui::code::create(body,240,164,source,ui::code::lineNumbers(source));
        lv_obj_set_style_bg_color(view.viewport,bg(),0);lv_obj_set_style_bg_color(view.gutter,surface(),0);
        lv_obj_set_style_text_color(view.source,ink(),0);lv_obj_set_style_text_color(view.numbers,muted(),0);lv_obj_set_style_border_color(view.gutter,edge(),0);lv_obj_set_style_bg_color(view.viewport,cyan(),LV_PART_SCROLLBAR);
        label(footer,"option="+std::to_string(stage)+" / 上下左右拖动",8,11,224);return;
    }
    if(page==Page::Result){
        auto star=label(body,LV_SYMBOL_OK,103,1,40);lv_obj_set_style_text_font(star,&lv_font_montserrat_24,0);lv_obj_set_style_text_color(star,cyan(),0);
        auto done=label(body,"交付成功",8,31,224);lv_obj_set_style_text_align(done,LV_TEXT_ALIGN_CENTER,0);
        auto card=panel(body,8,58,224,98);lv_obj_set_style_bg_color(card,surface(),0);lv_obj_set_style_radius(card,10,0);
        const auto &run=lesson->runs[stage];
        auto result=label(card,"Java / 工作台："+run.output,8,7,208);
        lv_label_set_long_mode(result,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        label(card,play::Missions[index].lesson,8,31,208,true);
        button(footer,"再玩",Action::Retry,0,4,1,56,38);button(footer,"Java",Action::Code,0,64,1,56,38);
        button(footer,stage<2?"下个任务":index+1<p::LessonCount?"下一站":"返回工坊",Action::Advance,0,124,1,112,38,true);return;
    }
}
void DesignPatterns::perform(unsigned input){
    if(page!=Page::Workshop||input>4)return;
    if(input<4&&!game.editable(input))return;
    if(historySize==history.size()){std::move(history.begin()+1,history.end(),history.begin());--historySize;}
    history[historySize++]=game.v;game.act(input);guide();
    if(game.route.size){page=Page::Running;frame=0;stepAt=frameAt=lv_tick_get();paused=false;}
    else if(game.won){finish();return;}
    draw();
}
void DesignPatterns::finish(){
    if(!game.won){page=Page::Workshop;draw();return;}
    passed=true;progress.complete[index]|=uint8_t(1u<<stage);markDirty();flush();page=Page::Result;draw();
}
void DesignPatterns::undo(){
    if(page!=Page::Workshop||!historySize)return;
    game.v=history[--historySize];game.route={};game.won=game.error=false;game.say("已撤销上一步");guide();draw();
}
void DesignPatterns::primary(){
    if(page==Page::Workshop)act(Action::Run);else if(page==Page::Brief)act(Action::Start);
    else if(page==Page::Running&&paused)act(Action::Resume);else if(page==Page::Result)act(Action::Advance);else if(page==Page::Home)act(Action::Continue);
}
bool DesignPatterns::onBack(){
    if(page==Page::Home)return false;
    if(job){job->cancelled.store(true);job.reset();}
    if(page==Page::Code)page=codeReturn;
    else if(page==Page::Info)page=infoReturn;
    else if(page==Page::Running){paused=false;if(game.won){finish();return true;}page=Page::Workshop;guide();}
    else if(page==Page::Help||page==Page::Groups)page=Page::Home;
    else if(page==Page::List){page=Page::Groups;lesson.reset();}
    else if(page==Page::Loading){page=Page::Home;ready=true;}
    else{flush();page=Page::List;group=p::Catalogs[index].group;listOffset=0;lesson.reset();historySize=0;}
    draw();return true;
}
void DesignPatterns::click(lv_event_t *e){
    auto self=static_cast<DesignPatterns *>(lv_event_get_user_data(e));auto value=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    self->act(Action(value&255),unsigned(value>>8));
}
void DesignPatterns::act(Action action,unsigned value){
    if(hidden)return;
    if(action==Action::Back){if(!onBack())ui::WindowManager::instance().goHome();return;}if(!timer)return;
    if(action==Action::Reload){keepSession=ready;loadProgress();return;}if(job)return;
    switch(action){
    case Action::Continue:if(!ready)return;atlas=false;request(progress.started?progress.lesson:0,progress.started?progress.stage:0);return;
    case Action::Groups:case Action::Atlas:atlas=action==Action::Atlas;page=Page::Groups;lesson.reset();break;
    case Action::Category:if(value>=4)return;group=value;listOffset=0;page=Page::List;break;
    case Action::Entry:if(value>=p::LessonCount)return;request(value,0);return;
    case Action::Prev:if(listOffset>=4)listOffset-=4;break;
    case Action::Next:{unsigned count=0;for(auto &catalog:p::Catalogs)if(catalog.group==group)++count;if(listOffset+4<count)listOffset+=4;break;}
    case Action::Help:page=Page::Help;break;
    case Action::Start:if(page!=Page::Brief)return;page=Page::Workshop;guide();break;
    case Action::Node:perform(value);return;
    case Action::Run:perform(4);return;
    case Action::Undo:undo();return;
    case Action::Hint:if(page!=Page::Workshop)return;hints=true;guide();break;
    case Action::Info:if(!lesson)return;if(page==Page::Running){paused=false;if(game.won)finish();else page=Page::Workshop;}infoReturn=page;page=Page::Info;break;
    case Action::Code:if(!lesson)return;codeReturn=page;page=Page::Code;break;
    case Action::Practice:if(!lesson)return;if(atlas){atlas=false;progress.started=true;markDirty();stagePage();}else page=infoReturn;break;
    case Action::Retry:case Action::Reset:if(!lesson)return;atlas=false;stagePage();break;
    case Action::Advance:
        if(page!=Page::Result||!passed)return;
        if(stage<2){++stage;markDirty();stagePage();}
        else if(index+1<p::LessonCount){request(index+1,0);return;}
        else{page=Page::Home;message="全部工作站已交付，可自由重玩";lesson.reset();}break;
    case Action::Resume:if(page!=Page::Running||!paused||!sys::Power::isScreenOn())return;paused=false;stepAt=lv_tick_get()-frame;frameAt=lv_tick_get();break;
    default:return;
    }draw();
}
}
