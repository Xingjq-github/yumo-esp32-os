#include "garden.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/garden_controls.hpp"
#include "ui/window_mgr.hpp"
#include "core/power_mgr.hpp"
#include "core/button_mgr.hpp"
#include "esp_random.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>

namespace apps {
namespace {
namespace g=core::garden;
namespace c=ui::compact;
namespace t=ui::theme;
namespace art=ui::garden_controls;
using Art=art::Art;
constexpr uint32_t Ink=0x405848,Paper=0xF9ECCB,Edge=0x8B9D75,Shadow=0xB3BB8E;
const char *Stages[]={"萌芽","幼年","少年","成年"};
constexpr int HomeHeaderHeight=45,HomePetTop=4+HomeHeaderHeight+2,HomePetBottom=186;
struct NameplateStyle {uint32_t ink,paper,light;};
constexpr NameplateStyle Nameplates[]={
    {0x42664E,0xF0EFD3,0xC5E59B},
    {0x8C5262,0xFAEADA,0xFFC6C5},
    {0x505E85,0xECEEDC,0xCBDCFF},
    {0x79562E,0xFAEDC8,0xFFE5A0}
};
const char *Decorations[]={"暖灯","小屋","书桌"};
const char *Notes[]={"花","雨","风","星"};
constexpr unsigned MemoryCountdown=3000,MemoryLit=900,MemoryBeat=1200;
bool pointerDown(){
    for(auto input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input)){
        if(lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER && lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)return true;
    }
    return false;
}
lv_obj_t *label(lv_obj_t *parent,const char *value,int x,int y,int w,bool wrap=false){
    auto obj=c::label(parent,value,w);lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_text_font(obj,t::fontMenu(),0);
    lv_obj_set_style_text_color(obj,lv_color_hex(Ink),0);
    if(wrap)lv_label_set_long_mode(obj,LV_LABEL_LONG_MODE_WRAP);
    return obj;
}
lv_obj_t *paper(lv_obj_t *parent,int x,int y,int w,int h,bool scroll=false){
    auto obj=c::panel(parent,w,h,scroll);lv_obj_set_pos(obj,x,y);
    lv_obj_set_clickable(obj,false);
    lv_obj_set_style_bg_color(obj,lv_color_hex(Paper),0);lv_obj_set_style_radius(obj,4,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(Ink),0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(Edge),LV_PART_SCROLLBAR);
    return obj;
}
lv_obj_t *picture(lv_obj_t *parent,const lv_image_dsc_t *source,int x,int y,int size){
    auto obj=lv_image_create(parent);lv_image_set_src(obj,source);lv_image_set_pivot(obj,0,0);
    lv_image_set_scale(obj,size*256/source->header.w);lv_image_set_antialias(obj,false);
    lv_obj_set_pos(obj,x,y);lv_obj_set_clickable(obj,false);return obj;
}
lv_obj_t *number(lv_obj_t *parent,unsigned value,int x,int y,int w){
    char text[24];std::snprintf(text,sizeof(text),"%u",value);
    auto obj=label(parent,text,x,y,w);lv_obj_set_style_text_font(obj,&lv_font_montserrat_12,0);
    return obj;
}
const char *storageError(int error){
    switch(error){
    case EBADMSG:return "存档校验失败，已有文件已保留。";
    case ENOTSUP:return "存档来自更新版本，已有文件已保留。";
    case ESTALE:return "存储已变化，请退出后重新进入。";
    case EBUSY:return "内存卡正忙，请稍后重试。";
    case ENOMEM:return "可用内存不足，请稍后重试。";
    default:return "无法读写内存卡，请检查卡片。";
    }
}
}
lv_obj_t *Garden::button(lv_obj_t *parent,const char *caption,int command,int x,int y,int w,int h,const char *detail){
    if(buttonCount>=bindings.size())return nullptr;
    auto &binding=bindings[buttonCount++];binding={this,command};
    auto obj=lv_button_create(parent);lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj,w,h);lv_obj_set_pos(obj,x,y);lv_obj_set_scrollable(obj,false);
    lv_obj_add_event_cb(obj,touch,LV_EVENT_CLICKED,&binding);
    binding.object=obj;
    // A transparent, fixed-size hit area covers the pet; animation owns no objects.
    if(command==Interact)return obj;
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_bg_color(obj,lv_color_hex(Paper),0);
    lv_obj_set_style_border_width(obj,2,0);lv_obj_set_style_border_color(obj,lv_color_hex(Edge),0);
    lv_obj_set_style_radius(obj,4,0);lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0xE3C995),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0xFFE6A0),LV_STATE_CHECKED);
    lv_obj_set_style_border_color(obj,lv_color_hex(0xB98D55),LV_STATE_CHECKED);
    lv_obj_set_style_opa(obj,LV_OPA_50,LV_STATE_DISABLED);
    Art icon=Art::Leaf;
    switch(command){
    case Back:icon=Art::Back;break;case ToHome:case Adopt:icon=Art::Heart;break;
    case ToCare:case Feed:icon=Art::Fruit;break;case ToTrain:icon=Art::Sprout;break;
    case ToExplore:case Continue:icon=Art::Map;break;case ToHouse:icon=Art::House;break;
    case ToAlbum:icon=Art::Album;break;case Rest:icon=Art::Bed;break;case Clean:icon=Art::Bubbles;break;
    case Cuddle:icon=Art::Heart;break;case Cook:icon=Art::Pot;break;case Play:icon=Art::Music;break;
    case Plant:icon=Art::Seed;break;case Harvest:icon=Art::Fruit;break;
    case Replay:icon=Art::Replay;break;case ToRebirth:case Confirm:icon=Art::Egg;break;
    case Retry:icon=Art::Save;break;case Temporary:icon=Art::Leaf;break;default:break;
    }
    constexpr Art routes[]={Art::Sprout,Art::Shield,Art::Book,Art::Compass,Art::Music,Art::Crystal};
    constexpr Art places[]={Art::Forest,Art::Sea,Art::Cloud};
    constexpr Art melody[]={Art::Flower,Art::Rain,Art::Wind,Art::Star};
    constexpr Art decor[]={Art::Lamp,Art::House,Art::Desk};
    if(command>=Training && command<Training+6)icon=routes[command-Training];
    if(command>=Exploring && command<Exploring+3)icon=places[command-Exploring];
    if(command>=Guessing && command<Guessing+4)icon=melody[command-Guessing];
    if(command>=Decorating && command<Decorating+3)icon=decor[command-Decorating];
    if(command>=Choosing && command<Choosing+2 && state.event!=g::None){
        const auto &event=g::Encounters[state.event];icon=routes[command==Choosing?event.leftRoute:event.rightRoute];
    }
    const bool portrait=command>=Preview && command<Preview+int(g::Forms);
    const bool rune=command>=Guessing && command<Guessing+4;
    const auto source=portrait?art::pet(command-Preview):art::icon(icon);
    int tx=3,ty=(h-20)/2,tw=w-10;
    if(w<120 && h>=48){
        const int size=portrait?40:rune?36:h>=100?48:24;
        picture(obj,source,(w-size)/2-2,4,size);ty=portrait?47:rune?46:h>=100?61:30;
    }else if(w<48){picture(obj,source,(w-16)/2-2,1,16);ty=18;}
    else {
        const int size=h>=48?32:20;picture(obj,source,6,(h-size)/2-2,size);
        tx=size+13;tw=w-tx-7;if(detail)ty=6;
    }
    auto title=label(obj,caption,tx,ty,tw);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    if(detail){
        const int sy=w<120?(h>=100?85:50):h-24;
        auto sub=label(obj,detail,tx,sy,tw);lv_obj_set_style_text_align(sub,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_color(sub,lv_color_hex(0x788360),0);
    }
    if(rune){
        // Read-only during the demonstration, but never faded by the disabled style.
        lv_obj_set_style_opa(obj,LV_OPA_COVER,LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(obj,lv_color_hex(0xA8D5A2),LV_STATE_CHECKED);
        lv_obj_set_style_border_color(obj,lv_color_hex(0x355B44),LV_STATE_CHECKED);
        lv_obj_set_style_border_width(obj,4,LV_STATE_CHECKED);
    }
    return obj;
}
void Garden::header(const char *title,const char *detail){
    button(root(),"返回",Back,4,4,40,40);
    auto panel=paper(root(),48,4,188,40);
    auto name=label(panel,title,6,*detail?1:10,176);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    if(*detail){auto sub=label(panel,detail,6,21,176);lv_obj_set_style_text_align(sub,LV_TEXT_ALIGN_CENTER,0);}
}
void Garden::homeHeader(const char *name){
    button(root(),"返回",Back,4,4,40,40);
    const unsigned stage=g::stage(state);const auto &style=Nameplates[stage];
    // Two rows keep every value visible in two thirds of the previous height.
    nameplate=paper(root(),48,4,188,HomeHeaderHeight);
    lv_obj_set_style_bg_color(nameplate,lv_color_hex(style.ink),0);
    lv_obj_set_style_radius(nameplate,6,0);
    auto title=label(nameplate,temporary?"临时伙伴":name,5,3,58);
    lv_obj_set_style_text_color(title,lv_color_hex(0xFFF8E5),0);
    auto badge=paper(nameplate,66,3,32,18);
    lv_obj_set_style_bg_color(badge,lv_color_hex(style.light),0);
    auto phase=label(badge,Stages[stage],2,0,28);
    lv_obj_set_style_text_align(phase,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(phase,lv_color_hex(style.ink),0);
    picture(nameplate,art::icon(Art::Fruit),103,5,12);
    auto fruit=number(nameplate,state.fruit,117,5,24);lv_obj_set_style_text_color(fruit,lv_color_hex(0xFFF8E5),0);
    picture(nameplate,art::icon(Art::Star),144,5,12);
    auto stars=number(nameplate,state.stars,158,5,24);lv_obj_set_style_text_color(stars,lv_color_hex(0xFFF8E5),0);
    auto details=paper(nameplate,2,23,184,20);
    lv_obj_set_style_bg_color(details,lv_color_hex(style.paper),0);
    const unsigned values[]={state.hunger,state.mood,state.energy};
    const Art icons[]={Art::Fruit,Art::Heart,Art::Leaf};
    const uint32_t colors[]={0xCA865E,0xC96987,0x658D56};
    for(unsigned i=0;i<3;++i){
        const int x=4+60*i;picture(details,art::icon(icons[i]),x,2,14);
        auto value=number(details,values[i],x+18,1,35);lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_CENTER,0);
        auto bar=lv_bar_create(details);lv_obj_remove_style_all(bar);lv_obj_set_pos(bar,x+18,16);lv_obj_set_size(bar,35,2);
        lv_obj_set_clickable(bar,false);
        lv_bar_set_value(bar,values[i],LV_ANIM_OFF);lv_obj_set_style_bg_color(bar,lv_color_hex(Shadow),LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar,lv_color_hex(colors[i]),LV_PART_INDICATOR);
    }
    // Fixed ornament objects: no per-frame allocation, timer or touch target.
    for(auto &spark:nameSparks){
        spark=paper(nameplate,0,6,stage==2?3:stage==3?9:2,stage==2?3:stage==3?1:2);
        lv_obj_set_style_bg_color(spark,lv_color_hex(style.light),0);
        lv_obj_set_style_radius(spark,stage<2?1:0,0);
        if(stage==2){
            lv_obj_set_style_bg_opa(spark,LV_OPA_TRANSP,0);
            for(unsigned axis=0;axis<2;++axis){
                auto ray=paper(spark,axis?0:1,axis?1:0,axis?3:1,axis?1:3);
                lv_obj_set_style_bg_color(ray,lv_color_hex(style.light),0);
            }
        }
    }
}
void Garden::animateNameplate(){
    if(!nameplate || page!=Page::Home || drawnPage!=page)return;
    const unsigned stage=g::stage(state),phase=frame%32;
    for(unsigned i=0;i<nameSparks.size();++i){
        const unsigned step=(phase+i*10)%32,wave=step<16?step:31-step;
        int x=i==0?0:i==1?186:24+int(step)*4,y=21;unsigned opacity=128+wave*8;
        switch(stage){
        case 0:if(i<2)y=19-int(step/2);break; // Tiny lights rise on the side edges.
        case 1:if(i<2)y=4+step/2;break; // Petals drift down beside the text.
        case 2:x=i==0?0:i==1?185:110;y=i<2?9:20;break; // Stars stay in the side and divider gaps.
        default:x=5+int(step)*5;y=i==1?21:i==2?HomeHeaderHeight-1:0;opacity=224;break;
        }
        lv_obj_set_pos(nameSparks[i],x,y);lv_obj_set_style_opa(nameSparks[i],lv_opa_t(opacity),0);
    }
}
void Garden::resetMemory(){miniElapsed=0;miniFrom=state.miniPosition;miniReset=true;miniTick=lv_tick_get();}
bool Garden::memoryWatching() const {
    return state.miniLength && miniElapsed<MemoryCountdown+(state.miniLength-miniFrom)*MemoryBeat;
}
void Garden::onCreate(lv_obj_t *parent){
    lv_obj_set_style_bg_color(parent,lv_color_hex(0xB7C993),0);setRoot(parent);
    image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_RGB565;
    image.header.w=ui::garden_scene::Width;image.header.h=ui::garden_scene::Height;
    image.header.stride=ui::garden_scene::Width*2;image.data_size=sizeof(pixels);
    image.data=reinterpret_cast<const uint8_t *>(pixels.data());
    timer=lv_timer_create(tick,30,this);lv_timer_pause(timer);draw();
}
void Garden::onShow(){
    if(visible)return;
    visible=true;backSession=sys::Buttons::claimBootBack();lv_timer_resume(timer);
    if(page==Page::Memory)resetMemory();
    if(!loaded && !job)beginLoad();
}
void Garden::onHide(){
    visible=false;pendingCount=0;if(timer)lv_timer_pause(timer);
    sys::Buttons::releaseBootBack(backSession);backSession=0;
    if(job && !job->saving)job->cancelled.store(true);
}
void Garden::onDestroy(){
    onHide();if(timer){lv_timer_delete(timer);timer=nullptr;}
    job.reset();scene=status=memoryStatus=toast=nullptr;notes.fill(nullptr);petActive=false;
    countdown=countdownText=nullptr;
    nameplate=nullptr;nameSparks.fill(nullptr);
}
bool Garden::onBack(){
    if(page==Page::Home || page==Page::Loading || page==Page::Welcome || page==Page::Error)return false;
    // In-progress encounters are preserved; Home offers an explicit Continue button.
    show(page==Page::Detail?Page::Album:Page::Home);return true;
}
void Garden::show(Page next){
    if(next!=page)pendingCount=0;
    if(next==Page::Memory && next!=page)resetMemory();
    page=next;refreshPending=true;petActive=false;
}
void Garden::beginLoad(){
    page=Page::Loading;error=0;loadSince=lv_tick_get();job=g::open();refreshPending=true;
}
void Garden::persist(){
    if(temporary){dirty=false;return;}
    job=g::save(state,revision,generation,access);
    if(!job){error=g::idle()?ENOMEM:EBUSY;saveError=true;show(Page::Error);}
}
void Garden::poll(){
    if(job && job->done.load(std::memory_order_acquire)){
        auto completed=job;job.reset();error=completed->error;
        if(error){saveError=completed->saving;show(Page::Error);}
        // A save acknowledgement changes controls/feedback, never their hit targets.
        else if(completed->saving){revision=completed->result.revision;dirty=false;saveError=false;}
        else {
            loaded=true;access=completed->access;generation=completed->generation;revision=completed->result.revision;
            if(completed->result.exists){
                state=completed->result.state;notice=completed->result.recovered?"已从备用存档恢复":"";noticeSince=lv_tick_get();
                show(state.event!=g::None?Page::Event:state.miniLength?Page::Memory:Page::Home);
            }else {state=g::create(esp_random());show(Page::Welcome);}
        }
    }
    if(page==Page::Loading && !job){
        if(lv_tick_elaps(loadSince)>3000){error=EBUSY;saveError=false;show(Page::Error);}
        else if(g::idle())job=g::open();
    }
}
void Garden::change(g::Action action,unsigned choice){
    if(job || dirty)return;
    const bool adult=state.form!=g::None;const auto outcome=g::apply(state,action,choice);
    notice=outcome.message;noticeSince=lv_tick_get();
    if(!outcome.changed){refreshPending=true;return;}
    dirty=true;
    if(action==g::Action::Play)show(Page::Memory);
    else if(state.event!=g::None)show(Page::Event);
    else if(state.miniLength)show(Page::Memory);
    else if(!adult && state.form!=g::None)show(Page::Adult);
    else if(action==g::Action::Rebirth || action==g::Action::Choose || action==g::Action::Guess)show(Page::Home);
    else refreshPending=true;
    persist();
}
void Garden::touch(lv_event_t *event){
    auto binding=*static_cast<Binding *>(lv_event_get_user_data(event));
    if(binding.owner->visible && binding.owner->page==binding.owner->drawnPage){
        sys::Power::activity();binding.owner->dispatch(binding.command);
    }
}
void Garden::dispatch(int command){
    if(command==Interact){petStart=lv_tick_get();petActive=true;petVariant=(petVariant+1)%3;animate(true);return;}
    if(command==Back){if(!onBack())ui::WindowManager::instance().goHome();return;}
    if(command==Retry){if(job)return;if(saveError){error=0;show(Page::Home);persist();}else beginLoad();return;}
    if(command==Temporary){
        const bool continuing=saveError;temporary=true;dirty=false;loaded=true;saveError=false;error=0;
        if(!continuing){state=g::create(esp_random());show(Page::Welcome);}else show(Page::Home);
        notice="临时体验，退出后不保存";return;
    }
    if(command==Adopt){if(job || dirty)return;dirty=true;notice.clear();show(Page::Home);persist();return;}
    if(command==Continue){show(state.event!=g::None?Page::Event:Page::Memory);return;}
    if(command==Replay){pendingCount=0;resetMemory();controls();animate();return;}
    if(command==ToHome){show(Page::Home);return;}
    if(command==ToCare){show(Page::Care);return;}
    if(command==ToTrain){show(Page::Train);return;}
    if(command==ToExplore){show(Page::Explore);return;}
    if(command==ToHouse){show(Page::House);return;}
    if(command==ToAlbum){show(Page::Album);return;}
    if(command==ToRebirth){show(Page::Rebirth);return;}
    if(command>=Preview && command<Preview+int(g::Forms)){preview=command-Preview;show(Page::Detail);return;}
    if(command>=Guessing && command<Guessing+4 && memoryWatching())return;
    if(job || dirty){
        // Queue only current-page game actions. Adoption/rebirth stay single-shot.
        if(job && job->saving && command>=Feed && command<Preview && pendingCount<pendingCommands.size()){
            pendingCommands[pendingCount++]=command;controls();feedback();
        }
        return;
    }
    if(command==Confirm){change(g::Action::Rebirth);return;}
    if(command>=Training && command<Training+6){change(g::Action::Train,command-Training);return;}
    if(command>=Exploring && command<Exploring+3){change(g::Action::Explore,command-Exploring);return;}
    if(command>=Choosing && command<Choosing+2){change(g::Action::Choose,command-Choosing);return;}
    if(command>=Guessing && command<Guessing+4){
        if(!memoryWatching())change(g::Action::Guess,command-Guessing);
        return;
    }
    if(command>=Decorating && command<Decorating+3){change(g::Action::Decorate,command-Decorating);return;}
    switch(command){
    case Feed:change(g::Action::Feed);break;case Rest:change(g::Action::Rest);break;
    case Clean:change(g::Action::Clean);break;case Cuddle:change(g::Action::Cuddle);break;
    case Cook:change(g::Action::Cook);break;case Play:change(g::Action::Play);break;
    case Plant:change(g::Action::Plant);break;case Harvest:change(g::Action::Harvest);break;default:break;
    }
}
void Garden::landscape(){
    auto viewport=c::panel(root(),240,240);
    lv_obj_set_clickable(viewport,false);
    scene=lv_image_create(viewport);lv_image_set_src(scene,&image);lv_image_set_pivot(scene,0,0);
    lv_image_set_scale(scene,512);lv_image_set_antialias(scene,false);lv_obj_set_pos(scene,0,0);
    lv_obj_set_clickable(scene,false);
}
void Garden::message(int y){
    // Keep the toast allocated so save completion never needs to rebuild a page.
    toast=paper(root(),6,y,228,22);status=label(toast,"",6,2,216);
    lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    feedback();
}
void Garden::feedback(){
    if(!toast)return;
    char waiting[64];std::snprintf(waiting,sizeof(waiting),"保存中 · 待处理 %u",pendingCount);
    const char *value=job && job->saving?(pendingCount?waiting:"保存中…"):
        temporary?"临时体验 · 退出后不保存":lv_tick_elaps(noticeSince)<2200?notice.c_str():"";
    c::setText(status,value);lv_obj_set_hidden(toast,!*value);
}
void Garden::draw(){
    refreshPending=false;drawnPage=page;scene=status=memoryStatus=toast=nullptr;notes.fill(nullptr);buttonCount=0;
    countdown=countdownText=nullptr;
    nameplate=nullptr;nameSparks.fill(nullptr);
    lv_obj_clean(root());char text[160];sceneView={};
    const auto r=g::route(state);
    const bool portrait=page==Page::Home || page==Page::Welcome || page==Page::Detail || page==Page::Adult;
    sceneView.pet=portrait;
    if(page==Page::Detail)sceneView.preview=preview;
    if(page==Page::Adult)sceneView.preview=state.form;
    if(page==Page::Detail || page==Page::Adult)sceneView.petSize=52;
    if(page==Page::Welcome){sceneView.ground=83;sceneView.petSize=54;}
    if(page==Page::Home){sceneView.ground=87;sceneView.petSize=60;}
    landscape();
    if(portrait)button(root(),"",Interact,page==Page::Home?28:54,page==Page::Home?HomePetTop:48,page==Page::Home?184:132,page==Page::Home?HomePetBottom-HomePetTop:114);
    switch(page){
    case Page::Loading:{
        header("萌芽物语");
        picture(root(),art::icon(Art::Seed),96,73,48);
        auto panel=paper(root(),18,144,204,42);
        auto title=label(panel,"正在打开花园…",8,12,188);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        break;}
    case Page::Welcome:{
        header("萌芽物语","初次相遇");
        auto title=label(root(),"24 回合，遇见新的自己",12,174,216);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        button(root(),"领养伙伴",Adopt,35,201,170,36);break;}
    case Page::Home:{
        const char *name=state.form==g::None?"萌芽物语":(state.form&1?g::RouteData[state.form/2].rare:g::RouteData[state.form/2].normal);
        homeHeader(name);
        const bool pending=state.event!=g::None || state.miniLength;
        button(root(),pending?"继续":"照料",pending?Continue:ToCare,3,187,46,50);
        button(root(),"培养",ToTrain,50,187,46,50);button(root(),"冒险",ToExplore,97,187,46,50);
        button(root(),"家园",ToHouse,144,187,46,50);button(root(),"图鉴",ToAlbum,191,187,46,50);
        if(pending){c::disabled(bindings[buttonCount-4].object,true);c::disabled(bindings[buttonCount-3].object,true);}
        message(163);break;}
    case Page::Care:{
        std::snprintf(text,sizeof(text),"果子 %u · 体力 %u",unsigned(state.fruit),unsigned(state.energy));header("照料背包",text);
        const char *captions[]={"喂食","休息","清洁","陪伴","料理","记忆"};
        const char *details[]={"1果 +31","体力+35","心情+20","心情+18","2果 +51","赢取星果"};
        for(int i=0;i<6;++i)button(root(),captions[i],Feed+i,6+i%3*78,56+i/3*79,72,74,details[i]);
        message();break;}
    case Page::Train:
        std::snprintf(text,sizeof(text),"体力 %u · 每次 -%u",unsigned(state.energy),g::TrainingEnergy);header("成长之路",text);
        for(int i=0;i<6;++i){
            std::snprintf(text,sizeof(text),"%u",unsigned(state.affinity[i]));
            auto b=button(root(),g::RouteData[i].practice,Training+i,6+i%3*78,56+i/3*79,72,74,text);
            if(unsigned(i)==r)c::checked(b,true);
        }
        message();break;
    case Page::Explore:
        header("旅途地图",g::Weather[state.weather]);
        for(int i=0;i<3;++i){
            const char *captions[]={"林间","海湾","云丘"};const char *details[]={"萤火古树","贝壳潮汐","风铃流星"};
            button(root(),captions[i],Exploring+i,6+i*78,66,72,117,details[i]);
        }
        message();break;
    case Page::Event:{
        if(state.event==g::None){show(Page::Home);break;}
        const auto &event=g::Encounters[state.event];header(event.title,"奇遇");
        auto dialog=paper(root(),6,51,228,73);label(dialog,event.story,9,9,210,true);
        button(root(),event.left,Choosing,6,134,228,47,g::RouteData[event.leftRoute].name);
        button(root(),event.right,Choosing+1,6,187,228,47,g::RouteData[event.rightRoute].name);
        break;}
    case Page::Memory:{
        header("回声花园");
        // Replay is a game control in the header, with four illustrated rune stones below.
        button(root(),"重看",Replay,196,4,40,40);
        auto heading=lv_obj_get_child(root(),lv_obj_get_child_count(root())-2);
        lv_obj_set_width(heading,144);auto title=lv_obj_get_child(heading,0);lv_obj_set_width(title,132);
        auto panel=paper(root(),18,50,204,24);memoryStatus=label(panel,"准备开始",4,3,196);
        lv_obj_set_style_text_align(memoryStatus,LV_TEXT_ALIGN_CENTER,0);
        for(int i=0;i<4;++i)notes[i]=button(root(),Notes[i],Guessing+i,28+i%2*98,82+i/2*78,86,70);
        countdown=paper(root(),69,95,102,105);lv_obj_set_style_bg_color(countdown,lv_color_hex(0x355B44),0);
        auto ready=label(countdown,"准备开始",5,8,92);lv_obj_set_style_text_color(ready,lv_color_white(),0);
        lv_obj_set_style_text_align(ready,LV_TEXT_ALIGN_CENTER,0);
        countdownText=label(countdown,"3",4,35,94);lv_obj_set_style_text_font(countdownText,&lv_font_montserrat_48,0);
        lv_obj_set_style_text_color(countdownText,lv_color_white(),0);lv_obj_set_style_text_align(countdownText,LV_TEXT_ALIGN_CENTER,0);
        break;}
    case Page::House:{
        std::snprintf(text,sizeof(text),"第 %u 代 · 星星 %u",unsigned(state.generation),unsigned(state.stars));header("林间家园",text);
        if(state.crop==g::None)button(root(),"播种",Plant,6,53,228,62,"2 果 · 4 回合成熟");
        else {
            if(state.crop==4)std::snprintf(text,sizeof(text),"获得 6 个果子");
            else std::snprintf(text,sizeof(text),"成长 %u/4",unsigned(state.crop));
            auto b=button(root(),state.crop==4?"收获":"生长中",Harvest,6,53,228,62,text);
            if(state.crop<4)c::disabled(b,true);
        }
        for(int i=0;i<3;++i){
            const char *detail=state.decor==i+1?"使用中":state.decorations&(1<<i)?"已拥有":"4 星";
            auto b=button(root(),Decorations[i],Decorating+i,6+i*78,122,72,74,detail);
            if(state.decor==i+1)c::checked(b,true);
        }
        if(state.form!=g::None)button(root(),"传承新生",ToRebirth,31,202,178,36);
        else message();
        break;}
    case Page::Album:{
        unsigned count=0;for(unsigned i=0;i<g::Forms;++i)count+=bool(state.album&(1<<i));
        std::snprintf(text,sizeof(text),"已相遇 %u/12",count);header("成长图鉴",text);
        auto list=c::panel(root(),232,190,true);lv_obj_set_pos(list,4,48);lv_obj_set_style_bg_opa(list,LV_OPA_TRANSP,0);
        lv_obj_set_style_bg_color(list,lv_color_hex(Edge),LV_PART_SCROLLBAR);
        for(int i=0;i<12;++i){
            const auto &route=g::RouteData[i/2];auto b=button(list,i&1?route.rare:route.normal,Preview+i,i%3*77,i/3*78,73,74);
            if(state.album&(1<<i))c::checked(b,true);
            else lv_obj_set_style_image_opa(lv_obj_get_child(b,0),LV_OPA_50,0);
        }
        break;}
    case Page::Detail:{
        const auto &route=g::RouteData[preview/2];
        std::snprintf(text,sizeof(text),"%s · %s",route.name,state.album&(1<<preview)?"已相遇":"未相遇");
        header(preview&1?route.rare:route.normal,text);
        auto dialog=paper(root(),6,169,228,66);
        label(dialog,preview&1?"路线倾向最高 · 平均照料 ≥65\n并带回该路线的信物。":"路线倾向最高即可成长。\n满足稀有条件将进化为稀有形态。",9,11,210,true);
        break;}
    case Page::Adult:{
        const auto &route=g::RouteData[state.form/2];header(state.form&1?route.rare:route.normal,"成长完成");
        auto panel=paper(root(),24,164,192,27);
        std::snprintf(text,sizeof(text),"平均照料 %u · 收录图鉴",g::care(state));
        auto title=label(panel,text,4,5,184);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        button(root(),"继续陪伴",ToHome,6,200,112,36);button(root(),"传承新生",ToRebirth,122,200,112,36);break;}
    case Page::Rebirth:{
        header("传承新生");picture(root(),art::icon(Art::Egg),94,49,52);
        auto panel=paper(root(),6,110,228,81);
        label(panel,"保留图鉴、星星与装饰。\n下一代继承当前路线 5 点倾向。\n当前伙伴的成长将结束。",9,9,210,true);
        button(root(),"开启下一代",Confirm,25,201,190,36);break;}
    case Page::Error:{
        header(saveError?"进度尚未保存":"花园无法打开");picture(root(),art::icon(Art::Alert),100,51,40);
        auto panel=paper(root(),6,99,228,84);label(panel,storageError(error),9,7,210,true);
        std::snprintf(text,sizeof(text),"错误 %d · 临时体验不写存档",error);label(panel,text,9,53,210,true);
        button(root(),saveError?"重试保存":"重新读取",Retry,6,197,112,40);
        button(root(),saveError?"临时继续":"临时体验",Temporary,122,197,112,40);break;}
    }
    controls();animate(true);
}
void Garden::controls(){
    const bool pending=state.event!=g::None || state.miniLength;
    const bool watching=page==Page::Memory && memoryWatching();
    for(unsigned i=0;i<buttonCount;++i){
        const int cmd=bindings[i].command;bool disabled=false;
        if(cmd==Adopt || cmd==Confirm)disabled=job || dirty;
        else if(cmd>=Feed && cmd<Preview)disabled=pendingCount>=pendingCommands.size();
        if((cmd==ToTrain || cmd==ToExplore) && page==Page::Home && pending)disabled=true;
        if(cmd==Harvest && state.crop<4)disabled=true;
        if(cmd>=Guessing && cmd<Guessing+4 && watching)disabled=true;
        c::disabled(bindings[i].object,disabled);
    }
}
void Garden::animate(bool renderScene){
    if(petActive && lv_tick_elaps(petStart)>=1200)petActive=false;
    if(scene && renderScene){
        sceneView.reaction=petActive?1+lv_tick_elaps(petStart)/100:0;sceneView.variant=petVariant;
        ui::garden_scene::render(pixels,state,frame,sceneView);lv_obj_invalidate(scene);
        animateNameplate();
    }
    feedback();
    if(page==Page::Memory && drawnPage==page && state.miniLength){
        // Advance only while displayed. A delayed GUI frame cannot skip several cues.
        if(miniReset)miniReset=false;
        else if(memoryWatching())miniElapsed+=std::min(uint32_t(100),lv_tick_elaps(miniTick));
        miniTick=lv_tick_get();
        const bool counting=miniElapsed<MemoryCountdown,watching=memoryWatching();int active=-1;
        const unsigned step=counting?miniFrom:miniFrom+(miniElapsed-MemoryCountdown)/MemoryBeat;
        if(!counting && watching && (miniElapsed-MemoryCountdown)%MemoryBeat<MemoryLit)active=state.melody[step];
        for(int i=0;i<4;++i)if(notes[i])c::checked(notes[i],active==i);
        if(countdown){
            lv_obj_set_hidden(countdown,!counting);
            if(counting){char count[8];std::snprintf(count,sizeof(count),"%u",unsigned(3-miniElapsed/1000));c::setText(countdownText,count);}
        }
        char text[96];
        if(counting)std::snprintf(text,sizeof(text),"记住接下来的顺序");
        else if(watching)std::snprintf(text,sizeof(text),"看顺序 · %u/%u",step+1,unsigned(state.miniLength));
        else if(pendingCount)std::snprintf(text,sizeof(text),"待处理 %u · 保存中",pendingCount);
        else std::snprintf(text,sizeof(text),"轮到你 · %u/%u",unsigned(state.miniPosition+1),unsigned(state.miniLength));
        c::setText(memoryStatus,text);
    }
}
void Garden::tick(lv_timer_t *timer){
    auto self=static_cast<Garden *>(lv_timer_get_user_data(timer));if(!self->visible)return;
    self->poll();if(!sys::Power::isScreenOn()){
        if(self->page==Page::Memory)self->resetMemory();
        sys::Buttons::takeBootBack(self->backSession);lv_timer_set_period(timer,1000);return;
    }
    lv_timer_set_period(timer,30);
    if(sys::Buttons::takeBootBack(self->backSession)){self->dispatch(Back);return;}
    while(!self->job && !self->dirty && self->pendingCount){
        const int command=self->pendingCommands[0];
        for(unsigned i=1;i<self->pendingCount;++i)self->pendingCommands[i-1]=self->pendingCommands[i];
        --self->pendingCount;self->dispatch(command);
    }
    const bool held=pointerDown(),nextFrame=lv_tick_elaps(self->frameSince)>=100;
    if(nextFrame){++self->frame;self->frameSince=lv_tick_get();}
    // Do not destroy a pressed button between its down and up samples.
    if(self->refreshPending && !held)self->draw();
    else {
        self->controls();
        // Menu backgrounds are static; pet motion remains 10 fps and yields to touch.
        self->animate(nextFrame && self->sceneView.pet && !held);
    }
}
}
