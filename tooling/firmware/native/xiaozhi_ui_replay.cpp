// Real App + LVGL + button_mgr callbacks. Cloud and board effects are doubles.
#include "host_services.hpp"
#include "iot_button.h"
#include "apps/xiaozhi.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "ui/text_input.hpp"
#include "src/misc/lv_text_private.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

static unsigned checks=0,scenarios=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
using State=core::xiaozhi::State;
HostButton buttons[6];
namespace fake {
int64_t now=1000000;
bool screen=true,sleep=false,busy=false,panel=false;
int volume=50,starts=0,talks=0,cancels=0,shots=0,toggles=0,powerOffs=0;
uint32_t nextId=0;
struct Subscription {sys::EventType type;sys::EventHandler handler;};
std::map<uint32_t,Subscription> subscriptions;
std::vector<sys::Event> events;
std::unique_ptr<apps::Xiaozhi> active;
std::shared_ptr<core::xiaozhi::Job> job;
std::map<core::xiaozhi::Job *,core::xiaozhi::Snapshot> snapshots;
}
int64_t esp_timer_get_time(){return fake::now;}
int sys::Settings::volume(){return fake::volume;}
void sys::SystemService::setVolume(int v){fake::volume=v;}
void sys::Screenshot::request(){++fake::shots;}
uint32_t sys::EventBus::subscribe(EventType type,EventHandler handler){const auto id=++fake::nextId;fake::subscriptions.emplace(id,fake::Subscription{type,std::move(handler)});return id;}
void sys::EventBus::unsubscribe(uint32_t id){fake::subscriptions.erase(id);}
void sys::EventBus::publish(const Event &event){fake::events.push_back(event);}
bool sys::Power::isScreenOn(){return fake::screen;}
bool sys::Power::sleepPending(){return fake::sleep;}
bool sys::Power::wokeFromSleep(){return false;}
bool sys::Power::slowIdle(){return false;}
void sys::Power::activity(){}
void sys::Power::screenOn(){fake::screen=true;}
void sys::Power::toggleScreen(){fake::screen=!fake::screen;++fake::toggles;if(!fake::screen)EventBus::publish({EventType::ScreenOff});}
void sys::Power::powerOff(){++fake::powerOffs;}
namespace core::xiaozhi {
Snapshot Job::snapshot(){return fake::snapshots.at(this);}
std::shared_ptr<Job> start(){
    ++fake::starts;if(fake::busy)return {};
    fake::job=std::make_shared<Job>();fake::snapshots[fake::job.get()]={};return fake::job;
}
bool toggleTalk(const std::shared_ptr<Job> &job){
    if(!job || job->cancelled || job->done)return false;
    auto &state=fake::snapshots[job.get()];
    if(state.state==State::Ready)state.state=State::Listening;
    else if(state.state==State::Listening)state.state=State::Thinking;
    else if(state.state==State::Speaking)state.state=State::Ready;
    else return false;
    ++state.revision;++fake::talks;return true;
}
void cancel(const std::shared_ptr<Job> &job){if(job){job->cancelled=true;++fake::cancels;}}
bool idle(){return !fake::job || fake::job->done;}
}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
lv_obj_t *WindowManager::inputRoot()const{return fake::panel?nullptr:fake::active?fake::active->root():nullptr;}
void WindowManager::goHome(){
    if(!fake::active)return;
    auto root=fake::active->root();fake::active->onHide();CHECK(fake::active->powerHints()==sys::power::None);
    fake::active->onDestroy();fake::active.reset();lv_obj_delete(root);
}
void TextInput::bindField(lv_obj_t *){CHECK(false);}
}
static uint8_t pixels[240*240*2];
static lv_indev_t *touch=nullptr;static lv_point_t point{};static bool pressed=false;
static void dispatch(){
    auto events=std::move(fake::events);fake::events.clear();
    for(const auto &event:events){
        if(event.type==sys::EventType::HomeRequested){ui::WindowManager::instance().goHome();continue;}
        auto subscriptions=fake::subscriptions;
        for(auto &entry:subscriptions)if(entry.second.type==event.type && fake::subscriptions.count(entry.first))entry.second.handler(event);
    }
}
static void advance(){fake::now+=100000;lv_tick_inc(100);dispatch();lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void emit(int pin,button_event_t code){auto &b=buttons[pin];b.event=code;CHECK(b.cb[code]);b.cb[code](&b,nullptr);}
static void key(int pin){emit(pin,BUTTON_PRESS_DOWN);emit(pin,BUTTON_PRESS_UP);emit(pin,BUTTON_SINGLE_CLICK);emit(pin,BUTTON_PRESS_END);advance();}
static void twice(int pin){for(int i=0;i<2;++i){emit(pin,BUTTON_PRESS_DOWN);emit(pin,BUTTON_PRESS_UP);}emit(pin,BUTTON_DOUBLE_CLICK);emit(pin,BUTTON_PRESS_END);advance();}
static void tap(int x,int y){point={x,y};pressed=true;lv_tick_inc(20);lv_indev_read(touch);pressed=false;lv_tick_inc(20);lv_indev_read(touch);advance();}
static void hold(int x,int y){point={x,y};pressed=true;lv_tick_inc(20);lv_indev_read(touch);}
static void release(){pressed=false;lv_tick_inc(20);lv_indev_read(touch);advance();}
static lv_obj_t *find(lv_obj_t *root,const char *text){
    if(!root || lv_obj_is_hidden(root))return nullptr;
    if(lv_obj_check_type(root,&lv_label_class) && !std::strcmp(lv_label_get_text(root),text))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto label=find(lv_obj_get_child(root,i),text))return label;
    return nullptr;
}
static lv_obj_t *find(const char *text){return find(fake::active?fake::active->root():nullptr,text);}
static lv_obj_t *findType(lv_obj_t *root,const lv_obj_class_t *type){
    if(!root)return nullptr;
    if(lv_obj_check_type(root,type))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto obj=findType(lv_obj_get_child(root,i),type))return obj;
    return nullptr;
}
static void expect(const char *text){if(!find(text))std::fprintf(stderr,"Missing visible label: %s\n",text);CHECK(find(text));}
static lv_obj_t *userLabel(const char *text){
    if(auto label=find(text))return label;
    auto label=find((std::string("我 · ")+text).c_str());CHECK(label);return label;
}
static lv_obj_t *dialogue(lv_obj_t *label){
    CHECK(label);auto body=lv_obj_get_parent(label);CHECK(body && lv_obj_is_scrollable(body));
    CHECK(lv_obj_get_height(body)>=140);lv_area_t bounds{};lv_obj_get_coords(body,&bounds);
    CHECK(bounds.x1>=0 && bounds.x2<240 && bounds.y1>=36 && bounds.y2<192);return body;
}
static void scrollTo(lv_obj_t *body,int32_t y){lv_obj_scroll_to_y(body,y,LV_ANIM_OFF);lv_obj_update_layout(body);}
static int32_t scrollEnd(lv_obj_t *body){return lv_obj_get_scroll_y(body)+lv_obj_get_scroll_bottom(body);}
static void atBottom(lv_obj_t *body){CHECK(std::abs(lv_obj_get_scroll_bottom(body))<=1);}
static unsigned timers(){unsigned count=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++count;return count;}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static void publish(State state,const char *notice="",const char *user="",const char *answer="",const char *code=""){
    auto &snapshot=fake::snapshots.at(fake::job.get());snapshot.state=state;++snapshot.revision;
    snapshot.notice=notice;snapshot.userText=user;snapshot.assistantText=answer;snapshot.activationCode=code;advance();
}
static void open(){
    CHECK(!fake::active);fake::screen=true;fake::sleep=fake::panel=false;
    auto root=ui::compact::panel(lv_screen_active(),240,240);
    fake::active=std::make_unique<apps::Xiaozhi>();fake::active->setRoot(root);fake::active->onCreate(root);fake::active->onShow();advance();
    CHECK(fake::subscriptions.size()==2);
}
static void close(){ui::WindowManager::instance().goHome();advance();CHECK(fake::subscriptions.empty());fake::job.reset();fake::snapshots.clear();}
static void glyphs(lv_obj_t *root){
    if(lv_obj_is_hidden(root))return;
    if(lv_obj_check_type(root,&lv_label_class)) {
        const auto value=lv_label_get_text(root);const auto font=lv_obj_get_style_text_font(root,LV_PART_MAIN);uint32_t index=0;
        while(value[index]){const auto cp=lv_text_encoded_next(value,&index);if(cp=='\n')continue;lv_font_glyph_dsc_t d{};
            if(!lv_font_get_glyph_dsc(font,&d,cp,0) || d.is_placeholder){std::fprintf(stderr,"Missing U+%04lx in %s\n",(unsigned long)cp,value);CHECK(false);}}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)glyphs(lv_obj_get_child(root,i));
}
static void layout(){
    auto root=fake::active->root();lv_obj_update_layout(root);unsigned controls=0;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i){auto obj=lv_obj_get_child(root,i);if(lv_obj_is_hidden(obj))continue;lv_area_t area{};lv_obj_get_coords(obj,&area);
        CHECK(area.x1>=0 && area.x2<240 && area.y1>=0 && area.y2<240);
        if(lv_obj_check_type(obj,&lv_button_class)){CHECK(area.x2-area.x1+1>=36 && area.y2-area.y1+1>=36);++controls;}
    }
    CHECK(controls==2);CHECK(lv_obj_get_style_bg_opa(root,LV_PART_MAIN)==LV_OPA_COVER);glyphs(root);
}
static void capture(const char *name,int theme){
    char path[80];std::snprintf(path,sizeof(path),"%s-%d.ppm",name,theme);lv_refr_now(nullptr);
    FILE *file=std::fopen(path,"wb");CHECK(file);std::fprintf(file,"P6\n240 240\n255\n");
    for(size_t i=0;i<sizeof(pixels);i+=2){unsigned v=pixels[i]|unsigned(pixels[i+1])<<8;uint8_t rgb[]={uint8_t((v>>11)*255/31),uint8_t(((v>>5)&63)*255/63),uint8_t((v&31)*255/31)};std::fwrite(rgb,1,3,file);}std::fclose(file);
}
static void readingScenarios(int theme){
    // These scenarios drive production App rendering and real LVGL scrolling;
    // cloud, microphone, speaker and physical touch remain platform doubles.
    static constexpr char question[]=
        "我准备周末和家人一起出去走走，希望路线比较轻松，途中能看风景，"
        "也有适合休息的地方。请按照出发、途中、午饭和返回四个阶段给出建议，"
        "考虑天气变化、饮水、交通和带小朋友时需要注意的事项。"
        "如果下午开始下雨，应该如何调整计划？请逐项说明原因。";
    const std::string first=
        "建议选择一条离家较近的公园路线。\n"
        "出发前查看天气，准备饮水和轻便雨具。上午先沿平缓步道慢慢走，"
        "每走一段就安排短暂休息。\n"
        "午饭选择附近方便到达的地方，避免行程太紧。下午可以根据体力"
        "继续散步，也可以提前返回。\n"
        "如果开始下雨，优先寻找安全的室内休息区，不要在树下停留。";
    static constexpr char extra[]="\n还可以提前确认返程交通，给家人留下充足的休息时间。";
    open();publish(State::Thinking,"",question);auto prompt=userLabel(question);auto body=dialogue(prompt);
    CHECK(lv_obj_get_scroll_y(body)==0 && lv_obj_get_scroll_bottom(body)>20);layout();capture("question",theme);++scenarios;
    // The first answer starts in view, even after a question taller than the panel.
    publish(State::Speaking,"",question,first.c_str());auto answer=find(first.c_str());CHECK(dialogue(answer)==body);
    lv_area_t answerBounds{},bodyBounds{};lv_obj_get_coords(answer,&answerBounds);lv_obj_get_coords(body,&bodyBounds);
    CHECK(lv_obj_get_scroll_y(body)>20);CHECK(answerBounds.y1>=bodyBounds.y1 && answerBounds.y1<=bodyBounds.y1+16);
    layout();capture("reply-start",theme);++scenarios;
    // Streaming follows only readers who were already at (or close to) the end.
    std::string reply=first+extra;auto y=lv_obj_get_scroll_y(body);
    publish(State::Speaking,"",question,reply.c_str());CHECK(lv_obj_get_scroll_y(body)==y);++scenarios;
    scrollTo(body,scrollEnd(body));atBottom(body);y=lv_obj_get_scroll_y(body);
    reply+=extra;publish(State::Speaking,"",question,reply.c_str());atBottom(body);CHECK(lv_obj_get_scroll_y(body)>y);++scenarios;
    scrollTo(body,scrollEnd(body)-8);CHECK(lv_obj_get_scroll_bottom(body)==8);
    reply+=extra;publish(State::Speaking,"",question,reply.c_str());atBottom(body);capture("reply-bottom",theme);++scenarios;
    // Reading an earlier paragraph is stable across appended output.
    scrollTo(body,22);y=lv_obj_get_scroll_y(body);CHECK(lv_obj_get_scroll_bottom(body)>12);
    reply+=extra;publish(State::Speaking,"",question,reply.c_str());CHECK(lv_obj_get_scroll_y(body)==y);
    capture("reply-reading",theme);++scenarios;
    // A finger resting at the end must suppress auto-follow as content arrives.
    scrollTo(body,scrollEnd(body));atBottom(body);hold(120,135);y=lv_obj_get_scroll_y(body);
    reply+=extra;publish(State::Speaking,"",question,reply.c_str());CHECK(lv_obj_get_scroll_y(body)==y);
    CHECK(lv_obj_get_scroll_bottom(body)>12);release();++scenarios;
    // Real pointer dragging must still scroll, and output must not reset it.
    scrollTo(body,60);hold(120,155);hold(120,120);hold(120,80);
    CHECK(lv_indev_get_scroll_obj(touch)==body && lv_obj_is_scrolling(body));y=lv_obj_get_scroll_y(body);CHECK(y>60);
    reply+=extra;publish(State::Speaking,"",question,reply.c_str());CHECK(lv_obj_get_scroll_y(body)==y);release();
    lv_obj_stop_scroll_anim(body);++scenarios;
    // Level-only revisions update the meter without changing text or reading position.
    publish(State::Listening,"",question,reply.c_str());scrollTo(body,22);
    answer=find(reply.c_str());prompt=userLabel(question);const auto answerText=lv_label_get_text(answer),promptText=lv_label_get_text(prompt);
    auto meter=findType(fake::active->root(),&lv_bar_class);CHECK(meter);y=lv_obj_get_scroll_y(body);
    for(unsigned level:{0u,12u,78u,100u,140u,3u}){
        auto &snapshot=fake::snapshots.at(fake::job.get());++snapshot.revision;snapshot.level=level;advance();
        CHECK(lv_bar_get_value(meter)==static_cast<int32_t>(std::min(level,100u)));
        CHECK(lv_obj_get_scroll_y(body)==y && lv_label_get_text(answer)==answerText && lv_label_get_text(prompt)==promptText);
    }
    ++scenarios;
    publish(State::Thinking,"","第二个问题：请给出简短建议。");body=dialogue(userLabel("第二个问题：请给出简短建议。"));
    CHECK(lv_obj_get_scroll_y(body)==0);CHECK(!find(reply.c_str()));++scenarios;
    // A long failure is readable and never exposes the previous assistant answer.
    static constexpr char failure[]=
        "网络连接暂时不可用。请检查设备已经连接无线网络，并确认网络可以访问小智服务。"
        "如果连接仍然失败，可以稍后重试。正在退出音频会话，请等待任务结束后再点击重试。"
        "如果设备此前已经完成绑定，无需重新输入绑定码。";
    publish(State::Failed,failure,question,"不应继续显示的旧回复");expect(failure);CHECK(!find("不应继续显示的旧回复"));
    body=dialogue(find(failure));CHECK(lv_obj_get_scroll_y(body)==0 && lv_obj_get_scroll_bottom(body)>0);layout();capture("failed-long",theme);
    scrollTo(body,scrollEnd(body));atBottom(body);++scenarios;
    close();
}
int main(){
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *data){data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;data->point=point;});
    sys::Buttons::init();const auto baselineTimers=timers();
    key(4);CHECK(fake::volume==55);key(0);CHECK(fake::volume==50);twice(4);CHECK(fake::shots==1);++scenarios;
    for(int theme:{4,5,6,8,10}){
        ui::theme::init(theme);size_t baselineMemory=0;
        for(int cycle=0;cycle<6;++cycle){
            fake::busy=false;open();expect("连接中");layout();CHECK(fake::active->powerHints()==(sys::power::StayLit|sys::power::KeepSession));
            if(!cycle)capture("connecting",theme);
            publish(State::Activating,"绑定后即可对话","","","123456");expect("123456");layout();CHECK(fake::active->powerHints()!=0);
            if(!cycle)capture("activation",theme);
            const auto talks=fake::talks;key(4);CHECK(fake::talks==talks);
            publish(State::Ready);expect("开始说话");CHECK(fake::active->powerHints()==0);layout();
            if(!cycle)capture("ready",theme);
            key(4);expect("聆听中");CHECK(fake::talks==talks+1);CHECK(fake::active->powerHints()!=0);
            auto &snapshot=fake::snapshots[fake::job.get()];snapshot.level=78;advance();
            auto meter=findType(fake::active->root(),&lv_bar_class);CHECK(meter && lv_bar_get_value(meter)==78);layout();
            if(!cycle)capture("listening",theme);
            key(4);expect("思考中");CHECK(fake::talks==talks+2);
            layout();if(!cycle)capture("thinking",theme);
            publish(State::Speaking,"","今天适合散步吗？","今天可以到附近走一走，记得带水。这里是一段较长的回复，支持在窗口内滚动查看，不会遮挡底部说话按钮。");
            expect("打断");layout();if(!cycle)capture("speaking",theme);
            tap(120,212);expect("开始说话");CHECK(fake::talks==talks+3);
            const auto shot=fake::shots;twice(4);CHECK(fake::shots==shot+1);CHECK(fake::talks==talks+3);
            // A quick panel or stale event must not open the microphone underneath it.
            fake::panel=true;sys::Buttons::setSystemPanel(true);const auto volume=fake::volume;
            key(4);CHECK(fake::talks==talks+3 && fake::volume==std::min(volume+5,100));
            fake::panel=false;sys::Buttons::setSystemPanel(false);
            key(4);expect("聆听中");const auto starts=fake::starts;auto old=fake::job;
            // PWR still turns off the screen; cancellation is immediate on ScreenOff.
            key(5);CHECK(!fake::screen && old->cancelled);CHECK(fake::active->powerHints()==0);
            key(5);CHECK(fake::screen);expect("已暂停");layout();if(!cycle)capture("stopped",theme);key(4);CHECK(fake::starts==starts);
            old->done=true;advance();key(4);CHECK(fake::starts==starts+1 && fake::job!=old);
            publish(State::Failed,"网络断开，请重试","问题","旧回复");expect("网络断开，请重试");CHECK(!find("旧回复"));layout();if(!cycle)capture("failed",theme);
            const auto n=fake::starts;key(4);CHECK(fake::starts==n);fake::job->done=true;advance();tap(120,212);CHECK(fake::starts==n+1);
            publish(State::Ready);auto latest=fake::job;
            if(cycle%2){twice(0);CHECK(!fake::active);}else {key(0);CHECK(!fake::active);}
            CHECK(latest->cancelled);CHECK(fake::subscriptions.empty());CHECK(timers()==baselineTimers);
            close();if(!cycle)baselineMemory=memory();else CHECK(memory()>=baselineMemory);
            scenarios+=10;
        }
        readingScenarios(theme);CHECK(timers()==baselineTimers && fake::subscriptions.empty());
    }
    // A KEY press that began in a departed App cannot trigger the replacement.
    open();publish(State::Ready);emit(4,BUTTON_PRESS_DOWN);tap(18,18);CHECK(!fake::active);open();publish(State::Ready);
    const auto before=fake::talks;emit(4,BUTTON_PRESS_UP);emit(4,BUTTON_SINGLE_CLICK);emit(4,BUTTON_PRESS_END);advance();CHECK(fake::talks==before);close();++scenarios;
    // Failed task allocation remains visible and retries only after explicit input.
    fake::busy=true;open();expect("服务忙，请稍后重试");advance();fake::busy=false;key(4);expect("连接中");close();++scenarios;
    // Sleep request can arrive before ScreenOff dispatch; polling still cancels.
    open();auto job=fake::job;fake::sleep=true;advance();CHECK(job->cancelled && fake::active->powerHints()==0);close();fake::sleep=false;++scenarios;
    key(4);const auto v=fake::volume;key(0);CHECK(fake::volume==v-5);key(5);CHECK(!fake::screen);key(5);CHECK(fake::screen);
    emit(5,BUTTON_PRESS_DOWN);emit(5,BUTTON_LONG_PRESS_START);emit(5,BUTTON_PRESS_UP);emit(5,BUTTON_PRESS_END);CHECK(fake::powerOffs==1);++scenarios;
    CHECK(timers()==baselineTimers && fake::subscriptions.empty());
    std::printf("Xiaozhi UI: %u scenarios, %u checks; 5 themes, production App/GPIO, real LVGL; cloud/device not exercised.\n",scenarios,checks);
}
