#include "chiptune.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "core/settings_store.hpp"
#include "core/system_service.hpp"
#include <algorithm>
#include <cstdio>
namespace apps {
namespace c=ui::compact;
using core::chip::State;
lv_obj_t *Chiptune::button(const char *text,int id,int x,int y,int width){
    auto b=c::button(pageRoot,text,click,this,width,39);lv_obj_set_pos(b,x,y);
    lv_obj_set_user_data(b,reinterpret_cast<void *>(intptr_t(id)));
    if(auto caption=lv_obj_get_child(b,0)){lv_obj_set_width(caption,width-12);lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);}
    return b;
}
void Chiptune::onCreate(lv_obj_t *parent){
    host=parent;lv_obj_set_scrollable(host,false);lv_obj_set_style_pad_all(host,0,0);
    refresh();timer=lv_timer_create(tick,100,this);
}
void Chiptune::onHide(){
    pending=-1;if(listing)listing->cancel.store(true);if(player){player->cancel.store(true);player->wake();}
    if(timer){lv_timer_delete(timer);timer=nullptr;}
    listing.reset();player.reset();std::vector<core::chip::Entry>().swap(entries);
}
void Chiptune::onDestroy(){onHide();}
bool Chiptune::onBack(){
    if(!playback)return false;
    pending=-1;
    if(player&&player->state.load()==State::Loading){player->cancel.store(true);player->wake();player.reset();selected=-1;}
    playback=false;show();return true;
}
void Chiptune::refresh(){
    if(listing)return;
    // Catalog refresh stops playback so indices cannot refer to a changed file.
    if(player){player->cancel.store(true);player->wake();}
    player.reset();selected=pending=-1;entries.clear();page=0;
    listing=core::chip::scan();notice=listing?"正在读取chiptune…":"无法启动读取，请稍后刷新";show();
}
void Chiptune::choose(int index){
    if(index<0||size_t(index)>=entries.size())return;
    playback=true;
    if(selected==index&&player&&!player->done.load()){
        if(player->state.load()==State::Stopped)player->play.store(true);
        player->wake();
        show();return;
    }
    if(player){player->cancel.store(true);player->wake();}
    player.reset();selected=index;pending=index;notice="正在加载…";waitUntil=lv_tick_get()+4000;
    show();poll();
}
void Chiptune::show(){
    if(pageRoot)lv_obj_delete(pageRoot);
    pageRoot=c::panel(host,240,240);lv_obj_set_pos(pageRoot,0,0);
    status=title=position=playButton=loopButton=previous=next=stopButton=volume=nullptr;
    for(auto &row:rows)row=nullptr;
    button(LV_SYMBOL_LEFT,1,0,0,39);
    title=c::label(pageRoot,playback&&selected>=0?entries[selected].title.c_str():"8Bit音乐",playback?192:150);lv_obj_set_pos(title,44,11);
    if(!playback){
        auto reload=button(LV_SYMBOL_REFRESH,2,200,0,40);c::disabled(reload,bool(listing));
        if(entries.empty()){
            status=c::label(pageRoot,notice.empty()?"没有乐谱\n请复制*.chip.json到\nSD卡 /chiptune 目录":notice.c_str(),224);
            lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_pos(status,8,65);
        }else for(int i=0;i<4;++i){
            const int index=page*4+i;if(size_t(index)>=entries.size())break;
            std::string text=std::to_string(index+1)+". "+entries[index].title;
            if(!entries[index].error.empty())text="! "+text;
            auto b=button(text.c_str(),100+i,0,40+i*40,240);
            rows[i]=b;
            lv_obj_set_style_text_align(lv_obj_get_child(b,0),LV_TEXT_ALIGN_LEFT,0);
            c::checked(b,index==selected&&player&&player->state.load()==State::Playing);
        }
        auto prev=button(LV_SYMBOL_LEFT,3,0,200,59),nextPage=button(LV_SYMBOL_RIGHT,4,180,200,60);
        c::disabled(prev,page==0||bool(listing));c::disabled(nextPage,size_t((page+1)*4)>=entries.size()||bool(listing));
        char text[40];std::snprintf(text,sizeof(text),"%d/%u%s",page+1,unsigned(std::max(size_t(1),(entries.size()+3)/4)),truncated?" 已截断":"");
        auto pages=c::label(pageRoot,text,118);lv_obj_set_pos(pages,61,211);lv_obj_set_style_text_align(pages,LV_TEXT_ALIGN_CENTER,0);
    }else {
        status=c::label(pageRoot,notice.c_str(),224);lv_obj_set_pos(status,8,43);
        lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        position=c::label(pageRoot,"00:00 / 00:00",224);lv_obj_set_pos(position,8,62);
        previous=button(LV_SYMBOL_PREV,5,0,80,79);playButton=button(LV_SYMBOL_PAUSE,6,80,80,79);next=button(LV_SYMBOL_NEXT,7,160,80,80);
        button(LV_SYMBOL_VOLUME_MID " -",8,0,120,79);volume=c::label(pageRoot,"",78);lv_obj_set_pos(volume,81,131);lv_obj_set_style_text_align(volume,LV_TEXT_ALIGN_CENTER,0);
        button(LV_SYMBOL_VOLUME_MAX " +",9,160,120,80);
        loopButton=button("循环",10,0,160,119);stopButton=button(LV_SYMBOL_STOP " 停止",11,120,160,120);
        auto foot=c::label(pageRoot,"",224);lv_obj_set_pos(foot,8,211);
        lv_obj_set_user_data(pageRoot,foot);
    }
}
void Chiptune::poll(){
    if(listing&&listing->done.load(std::memory_order_acquire)){
        if(listing->metadataOnly){
            for(size_t i=0;i<listing->entries.size();++i){
                const auto index=listing->offset+i;
                if(index<entries.size()&&entries[index].path==listing->entries[i].path)entries[index]=std::move(listing->entries[i]);
            }
            if(!listing->error.empty())notice=listing->error;
        }else {entries=std::move(listing->entries);notice=listing->error;truncated=listing->truncated;}
        listing.reset();show();
    }
    if(pending>=0){
        if(core::chip::idle())player=core::chip::start(entries[pending].path);
        if(player){pending=-1;notice.clear();}
        else if(int32_t(lv_tick_get()-waitUntil)>=0){pending=-1;notice="音频正在使用，请稍后重试";}
    }
    if(!playback){
        if(!listing){
            for(size_t i=page*4;i<std::min(entries.size(),size_t(page*4+4));++i)
                if(!entries[i].inspected){listing=core::chip::titles(entries,page*4);break;}
        }
        for(int i=0;i<4;++i)if(rows[i])c::checked(rows[i],page*4+i==selected&&player&&player->state.load()==State::Playing);
        return;
    }
    char text[100];const auto state=player?player->state.load():State::Loading;
    const bool ready=player&&state!=State::Loading&&state!=State::Failed&&!player->done.load();
    if(player&&state!=State::Loading){
        const unsigned now=player->position.load()/1000,total=player->duration.load()/1000;
        std::snprintf(text,sizeof(text),"%02u:%02u / %02u:%02u",now/60,now%60,total/60,total%60);c::setText(position,text);
        const char *message=state==State::Failed?player->error.c_str():state==State::Playing?"正在播放":state==State::Paused?"已暂停":"已停止";
        c::setText(status,message);
        if(!player->title.empty())c::setText(title,player->title.c_str());
    }else c::setText(status,notice.empty()?"正在加载…":notice.c_str());
    c::disabled(playButton,!ready);c::disabled(loopButton,!ready);c::disabled(stopButton,!ready);
    c::setText(lv_obj_get_child(playButton,0),state==State::Playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
    c::checked(loopButton,ready&&player->loop.load());
    c::disabled(previous,selected<=0);c::disabled(next,selected<0||size_t(selected+1)>=entries.size());
    std::snprintf(text,sizeof(text),"%d%%",sys::Settings::volume());c::setText(volume,text);
    std::snprintf(text,sizeof(text),"%d / %u    %u BPM",selected+1,unsigned(entries.size()),player?player->tempo.load():0);
    c::setText(static_cast<lv_obj_t *>(lv_obj_get_user_data(pageRoot)),text);
}
void Chiptune::action(int id){
    if(id==1){ui::WindowManager::instance().goBack();return;}
    if(id==2){refresh();return;}
    if(id==3||id==4){page+=id==3?-1:1;show();return;}
    if(id>=100){choose(page*4+id-100);return;}
    if(id==5||id==7){choose(selected+(id==5?-1:1));return;}
    if(id==8||id==9)sys::SystemService::setVolume(std::clamp(sys::Settings::volume()+(id==8?-5:5),0,100));
    if(player){
        if(id==6)player->play.store(!player->play.load());
        if(id==10)player->loop.store(!player->loop.load());
        if(id==11){player->play.store(false);player->rewind.store(true);}
        player->wake();
    }
    poll();
}
void Chiptune::click(lv_event_t *e){auto self=static_cast<Chiptune *>(lv_event_get_user_data(e));self->action(int(intptr_t(lv_obj_get_user_data(lv_event_get_target_obj(e)))));}
void Chiptune::tick(lv_timer_t *t){static_cast<Chiptune *>(lv_timer_get_user_data(t))->poll();}
}
