#include "programming.hpp"
#include "ui/compact_ui.hpp"
#include "ui/code_view.hpp"
#include "ui/theme.hpp"
#include "ui/text_input.hpp"
#include "core/button_mgr.hpp"
#include "core/event_bus.hpp"
#include "src/misc/lv_text_private.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
LV_FONT_DECLARE(lv_font_code_13);
namespace apps {
namespace {
constexpr const char *Categories[]={"Java","JVM","算法","MySQL","组件","工具"};
std::string lower(std::string text){for(auto &c:text)if(uint8_t(c)<128)c=char(std::tolower(uint8_t(c)));return text;}
std::string headerText(const std::string &text,int width,const lv_font_t *font) {
    lv_text_attributes_t attributes;lv_text_attributes_init(&attributes);
    size_t fit=0;
    while(fit<text.size() && text[fit]!='\n' && text[fit]!='\r') {
        size_t end=fit+1;
        while(end<text.size() && (uint8_t(text[end])&0xc0)==0x80)++end;
        if(lv_text_get_width(text.data(),uint32_t(end),font,&attributes)>width)break;
        fit=end;
    }
    return text.substr(0,fit);
}

}
lv_obj_t *Programming::button(lv_obj_t *parent,const char *text,Action action,unsigned value,int x,int y,int w,int h) {
    auto b=ui::compact::button(parent,text,click,this,w,h);lv_obj_set_pos(b,x,y);
    lv_obj_add_event_cb(b,[](lv_event_t *e){
        auto self=static_cast<Programming *>(lv_event_get_user_data(e));
        if(auto indev=lv_indev_active())lv_indev_get_point(indev,&self->m_press);
    },LV_EVENT_PRESSED,this);
    lv_obj_set_user_data(b,reinterpret_cast<void *>(uintptr_t(unsigned(action)|(value<<8))));
    if(auto t=lv_obj_get_child(b,0)){lv_obj_set_width(t,w-8);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(t,LV_LABEL_LONG_MODE_DOTS);}return b;
}
lv_obj_t *Programming::label(lv_obj_t *parent,const std::string &s,int x,int y,int w,bool wrap) {
    auto t=ui::compact::label(parent,s.c_str(),w);lv_obj_set_pos(t,x,y);
    lv_label_set_long_mode(t,wrap?LV_LABEL_LONG_MODE_WRAP:LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_event_bubble(t,true);return t;
}
void Programming::onCreate(lv_obj_t *parent) {
    m_parent=parent;lv_obj_set_style_bg_color(parent,ui::theme::bg(),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    lv_obj_set_scrollable(parent,false);lv_obj_set_gesture_bubble(parent,false);
    m_header=ui::compact::panel(parent,240,36);m_body=ui::compact::panel(parent,240,168);m_footer=ui::compact::panel(parent,240,36);
    lv_obj_set_pos(m_body,0,36);lv_obj_set_pos(m_footer,0,204);
    lv_obj_add_event_cb(m_body,[](lv_event_t *e) {
        auto self=static_cast<Programming *>(lv_event_get_user_data(e));
        if(self->m_drawing)return;
        if(self->m_page==Page::Reader)self->m_scroll[self->m_code]=std::max(0,int(lv_obj_get_scroll_y(self->m_body)));
        else if(self->count()){self->m_offset=std::max(0,int(lv_obj_get_scroll_y(self->m_body)));self->updateRows();}
    },LV_EVENT_SCROLL,this);
    lv_obj_add_event_cb(m_body,[](lv_event_t *e) {
        auto self=static_cast<Programming *>(lv_event_get_user_data(e));
        if(self->m_drawing)return;
        const int y=lv_obj_get_scroll_y(self->m_body),bottom=lv_obj_get_scroll_bottom(self->m_body);
        if(y<0 || bottom<0)lv_obj_scroll_to_y(self->m_body,std::max(0,y+std::min(0,bottom)),LV_ANIM_OFF);
        if(self->m_page==Page::Reader)self->mark();
    },LV_EVENT_SCROLL_END,this);
    lv_obj_add_event_cb(m_body,[](lv_event_t *e) {
        auto self=static_cast<Programming *>(lv_event_get_user_data(e));
        if(self->m_page==Page::Reader && self->m_full){self->m_full=false;self->draw();}
    },LV_EVENT_SHORT_CLICKED,this);
    m_timer=lv_timer_create([](lv_timer_t *t){static_cast<Programming *>(lv_timer_get_user_data(t))->poll();},100,this);
    lv_timer_pause(m_timer);
    m_prevSub=sys::EventBus::subscribe(sys::EventType::PreviousPage,[this](const sys::Event &e){if(m_keys && e.i32==int32_t(m_keys))turn(-1);});
    m_nextSub=sys::EventBus::subscribe(sys::EventType::NextPage,[this](const sys::Event &e){if(m_keys && e.i32==int32_t(m_keys))turn(1);});
}
void Programming::onShow(){m_hidden=false;m_keys=sys::Buttons::claimPageKeys();lv_timer_resume(m_timer);load();}
void Programming::releaseView(){sys::Buttons::releaseViewExit(m_power);m_power=0;}
void Programming::onHide() {
    if(m_hidden)return;
    if(m_page==Page::Reader)mark();
    m_hidden=true;ui::TextInput::hide();flush();
    if(m_job){m_job->cancelled.store(true);m_job.reset();}
    m_save.reset();m_waitOpen=false;releaseView();sys::Buttons::releasePageKeys(m_keys);m_keys=0;
    if(m_timer)lv_timer_pause(m_timer);
    // WindowManager destroys the App and its LVGL tree immediately after Hide.
    // Workers only own immutable snapshots; none retains this or any LVGL object.
    m_library.reset();m_article={};m_state={};std::vector<std::string>().swap(m_blocks);
    m_codeViewport=nullptr;m_codeX=0;m_pool={};
    std::vector<Position>().swap(m_positions);std::vector<unsigned>().swap(m_matches);std::vector<std::string>().swap(m_groups);
}
void Programming::onDestroy(){onHide();if(m_timer){lv_timer_delete(m_timer);m_timer=nullptr;}sys::EventBus::unsubscribe(m_prevSub);sys::EventBus::unsubscribe(m_nextSub);m_prevSub=m_nextSub=0;}
uint32_t Programming::powerHints()const{return m_page==Page::Reader?sys::power::StayLit|sys::power::LowLoad|sys::power::Dim:sys::power::None;}
void Programming::onPowerSaving(bool on){if(m_timer)lv_timer_set_period(m_timer,on?500:100);}
void Programming::dirty(){m_dirty=true;m_changedAt=lv_tick_get();m_saveAccess=core::sd::requestAccess();}
void Programming::flush(){if(!m_dirty||!m_library)return;auto save=core::knowledge::save(m_state,m_library->generation,m_saveAccess);if(save){m_save=std::move(save);m_dirty=false;}else {m_changedAt=lv_tick_get();m_message="阅读记录暂未保存，请稍后重试。";}}
void Programming::load() {
    flush();if(m_job)m_job->cancelled.store(true);m_job.reset();releaseView();m_library.reset();m_article={};m_blocks.clear();m_full=false;
    m_codeX=0;
    m_matches.clear();m_groups.clear();m_positions.clear();m_return=m_origin=Page::Home;m_offset=m_listOffset=0;
    m_page=Page::Busy;m_waitOpen=true;m_waitAt=lv_tick_get();m_message="正在校验知识包…";draw();
}
void Programming::request(unsigned entry,unsigned section,size_t offset,uint32_t stableSection) {
    if(m_job || !m_library || entry>=m_library->count)return;
    m_restore=offset;m_restoreSection=stableSection;m_entry=entry;m_section=section;
    m_scroll[0]=m_scroll[1]=0;m_pendingRestore=true;
    m_codeX=0;
    // A failed or cancelled request must never expose an old block with the
    // new entry/section identifiers through the reading-options page.
    m_article={};m_blocks.clear();m_positions.clear();
    m_job=core::knowledge::read(m_library,entry,section,true);
    if(!m_job){releaseView();m_message="存储任务忙，请稍后重试。";m_page=Page::Error;draw();return;}
    m_message="正在读取章节…";m_page=Page::Busy;draw();
}
void Programming::poll() {
    if(m_hidden)return;
    if(sys::Buttons::takeViewExit(m_power)){onBack();return;}
    if(m_dirty && lv_tick_elaps(m_changedAt)>=1200)flush();
    if(m_save && m_save->done.load(std::memory_order_acquire)) {
        if(m_save->error && m_save->error!=ECANCELED){m_message="阅读记录未保存，请检查SD卡。";draw();}
        m_save.reset();
    }
    if(m_waitOpen && !m_job) {
        if(core::knowledge::idle())m_job=core::knowledge::open();
        if(m_job)m_waitOpen=false;
        else if(lv_tick_elaps(m_waitAt)>4000){m_waitOpen=false;m_message="存储任务忙或内存不足，请重试。";m_page=Page::Error;draw();}
    }
    if(!m_job || !m_job->done.load(std::memory_order_acquire))return;
    // A new read may start after the preceding worker releases its final owner.
    if(!core::knowledge::idle())return;
    auto job=std::move(m_job);
    if(job->error){releaseView();m_message=core::knowledge::errorText(job->error);m_page=Page::Error;draw();return;}
    if(job->operation==core::knowledge::Operation::Open) {
        m_library=std::move(job->library);m_state=std::move(job->state);m_message=job->stateDamaged?"阅读记录无效，知识内容仍可浏览。":"";
        m_page=Page::Home;m_offset=0;draw();return;
    }
    m_article=std::move(job->article);m_blocks=std::move(job->blocks);
    m_section=job->section;
    if(m_restoreSection) {
        auto found=std::find_if(m_article.sections.begin(),m_article.sections.end(),[this](const auto &s){return s.id==m_restoreSection;});
        m_section=found==m_article.sections.end()?0:unsigned(found-m_article.sections.begin());
        if(found==m_article.sections.end())m_restore=0;
        m_restoreSection=0;
    }
    m_hasCode=std::any_of(m_article.sections.begin(),m_article.sections.end(),[](const auto &s){return s.kind!=0;});
    m_code=m_article.sections[m_section].kind!=0;
    m_page=Page::Reader;m_message.clear();
    if(!m_power)m_power=sys::Buttons::claimViewExit();
    draw();mark();

}
void Programming::groups() {
    m_groups.clear();if(!m_library)return;
    for(unsigned i=0;i<m_library->count;++i){auto e=m_library->entry(i);if(e.category==m_category && std::find(m_groups.begin(),m_groups.end(),e.group)==m_groups.end())m_groups.emplace_back(e.group);}
}
void Programming::filter() {
    m_matches.clear();if(!m_library)return;
    if(m_mode==Mode::Recent){for(auto mark:m_state.recent){auto i=m_library->find(mark.id);if(i>=0)m_matches.push_back(unsigned(i));}return;}
    const auto query=lower(m_query);
    for(unsigned i=0;i<m_library->count;++i) {
        auto e=m_library->entry(i);bool match=false;
        if(m_mode==Mode::Favorites)match=m_state.favorite(e.id);
        else if(m_mode==Mode::Group)match=e.category==m_category && m_group==e.group;
        else {match=true;size_t start=0;while(start<query.size()){auto end=query.find(' ',start);auto token=query.substr(start,end==std::string::npos?end:end-start);if(!token.empty() && !strstr(e.search,token.c_str())){match=false;break;}if(end==std::string::npos)break;start=end+1;}}
        if(match)m_matches.push_back(i);
    }
}
size_t Programming::count()const {
    switch(m_page){case Page::Groups:return m_groups.size();case Page::List:return m_matches.size();case Page::Related:return m_article.related.size();default:return 0;}
}
void Programming::updateRows() {
    const unsigned first=unsigned(std::max(0,int(lv_obj_get_scroll_y(m_body))))/42;
    for(unsigned row=0;row<m_pool.size();++row) {
        auto b=m_pool[row];if(!b)continue;
        const unsigned i=first+row;
        if(i>=count()){lv_obj_set_hidden(b,true);continue;}
        lv_obj_set_hidden(b,false);
        std::string caption;Action action=Action::Entry;unsigned value=i;
        if(m_page==Page::Groups){caption=m_groups[i];action=Action::Group;}
        else if(m_page==Page::List){value=m_matches[i];caption=m_library->entry(value).title;}
        else {value=unsigned(m_library->find(m_article.related[i]));caption=m_library->entry(value).title;}
        caption=std::to_string(i+1)+". "+caption;
        lv_label_set_text(lv_obj_get_child(b,0),caption.c_str());
        lv_obj_set_user_data(b,reinterpret_cast<void *>(uintptr_t(unsigned(action)|(value<<8))));
        lv_obj_set_y(b,int(i)*42);
    }
}
void Programming::draw() {
    m_drawing=true;
    struct DrawingGuard{bool &value;~DrawingGuard(){value=false;}} guard{m_drawing};
    lv_obj_stop_scroll_anim(m_body);lv_indev_reset(nullptr,m_body);
    ui::TextInput::hide();m_input=nullptr;m_pool={};
    m_codeViewport=nullptr;
    lv_obj_clean(m_header);lv_obj_clean(m_body);lv_obj_clean(m_footer);
    lv_obj_set_layout(m_body,LV_LAYOUT_NONE);
    lv_obj_set_size(m_body,240,m_page==Page::Reader && m_full?240:204);
    lv_obj_set_pos(m_body,0,m_page==Page::Reader && m_full?0:36);lv_obj_scroll_to(m_body,0,0,LV_ANIM_OFF);
    lv_obj_set_scrollable(m_body,false);lv_obj_set_scroll_dir(m_body,LV_DIR_VER);
    lv_obj_set_hidden(m_header,false);lv_obj_set_hidden(m_footer,true);
    if(m_page==Page::Home || m_page==Page::Search || m_page==Page::Options || m_page==Page::Info || m_page==Page::Error) {
        lv_obj_set_height(m_body,168);lv_obj_set_hidden(m_footer,false);
    }
    lv_obj_set_scroll_elastic(m_body,false);
    button(m_header,LV_SYMBOL_LEFT,Action::Back,0,0,0,36,36);
    std::string title="编程速查";
    if(m_page==Page::Groups)title=Categories[m_category];
    if(m_page==Page::List)title=m_mode==Mode::Favorites?"收藏":m_mode==Mode::Recent?"最近阅读":m_mode==Mode::Search?"查找结果":m_group;
    if(m_page==Page::Reader || m_page==Page::Related)title=m_library->entry(m_entry).shortTitle;
    if(m_page==Page::Info)title="知识包信息";
    if(m_page==Page::Options)title="阅读设置";
    const int titleWidth=m_page==Page::Reader?80:128;
    const auto titleFont=ui::theme::fontSmall();
    auto heading=label(m_header,headerText(title,titleWidth,titleFont),38,(36-titleFont->line_height)/2,titleWidth);
    lv_obj_set_height(heading,titleFont->line_height);
    lv_label_set_long_mode(heading,LV_LABEL_LONG_MODE_CLIP);
    if(m_page==Page::Reader) {
        auto star=button(m_header,"",Action::Favorite,0,120,0,36,36);
        // Draw a five-point star: no dependency on Chinese/symbol font coverage.
        static const lv_point_precise_t points[]={{12,0},{15,8},{24,9},{17,15},{20,24},{12,19},{4,24},{7,15},{0,9},{9,8},{12,0}};
        auto shape=lv_line_create(star);lv_line_set_points(shape,points,11);lv_obj_center(shape);
        lv_obj_set_style_line_width(shape,2,0);lv_obj_set_style_line_color(shape,m_state.favorite(m_library->entry(m_entry).id)?ui::theme::onAccent():ui::theme::fg(),0);
        lv_obj_set_clickable(shape,false);
        ui::compact::checked(star,m_state.favorite(m_library->entry(m_entry).id));
        if(m_hasCode)button(m_header,m_code?"描述":"代码",Action::Toggle,0,158,0,44,36);
    }else button(m_header,"查",Action::Search,0,168,0,36,36);
    button(m_header,LV_SYMBOL_LIST,Action::Menu,0,204,0,36,36);
    if(m_page==Page::Busy || m_page==Page::Error) {
        label(m_body,m_message,6,12,228,true);
        if(m_page==Page::Error)button(m_footer,"重新加载",Action::Reload,0,0,0,240,36);
        return;
    }
    if(m_page==Page::Home) {
        for(unsigned i=0;i<6;++i)button(m_body,Categories[i],Action::Category,i,(i%2)*120,(i/2)*56,119,55);
        button(m_footer,"收藏",Action::Favorites,0,0,0,79,36);button(m_footer,"最近",Action::Recent,0,80,0,79,36);
        auto resume=button(m_footer,"继续",Action::Resume,0,160,0,80,36);ui::compact::disabled(resume,m_state.recent.empty());return;
    }
    if(m_page==Page::Search) {
        lv_obj_set_scrollable(m_body,true);lv_obj_set_flex_flow(m_body,LV_FLEX_FLOW_COLUMN);
        m_input=ui::compact::networkField(m_body,"标题 / 英文 / 拼音首字母",m_query.c_str(),64,ui::theme::fontSmall());
        label(m_body,"支持空格分隔关键词\n如 array、hashmap、dls。",0,40,236,true);
        button(m_footer,"查找",Action::Submit,0,0,0,240,36);return;
    }
    if(m_page==Page::Reader){drawReader();return;}
    if(m_page==Page::Info) {
        lv_obj_set_scrollable(m_body,true);
        std::string s="Java 21 / MySQL 8.4\n"+std::to_string(m_library->count)+" 个条目\n修订："+std::to_string(m_library->revision)+
            "\nSD目录：/programming\nlibrary.ykb为知识包，state.bin保存阅读记录。\nBOOT/KEY上下滚动，PWR返回。\n单击全屏显示操作栏；代码可左右拖动。\n仅供阅读，不执行代码。";
        if(!m_message.empty())s+="\n"+m_message;
        if(!m_article.sections.empty()) {
            auto e=m_library->entry(m_entry);
            s+="\n\n当前条目："+std::string(e.title)+"\n"+e.version+"\n"+e.source;
        }
        label(m_body,s,3,2,233,true);button(m_footer,"重新加载知识包",Action::Reload,0,0,0,240,36);return;
    }
    if(m_page==Page::Options) {
        button(m_body,m_state.wrap?"代码：自动换行":"代码：原行横移",Action::Wrap,0,0,0,240,40);
        button(m_body,m_state.large?"正文：较大字号":"正文：紧凑字号",Action::Size,0,0,42,240,40);
        auto full=button(m_body,"全屏阅读",Action::Full,0,0,84,240,40);ui::compact::disabled(full,m_blocks.empty());
        button(m_body,"知识包信息",Action::Info,0,0,126,240,40);
        button(m_footer,"返回首页",Action::Home,0,0,0,119,36);
        auto related=button(m_footer,"关联知识",Action::Related,0,120,0,120,36);ui::compact::disabled(related,m_article.related.empty());return;
    }
    const auto n=count();if(!n){label(m_body,"暂无条目",6,16,228);return;}
    lv_obj_set_scrollable(m_body,true);
    auto end=lv_obj_create(m_body);lv_obj_remove_style_all(end);lv_obj_set_size(end,1,1);lv_obj_set_pos(end,0,int(n)*42-1);
    lv_obj_set_clickable(end,false);lv_obj_set_scrollable(end,false);
    for(auto &b:m_pool) {
        b=button(m_body," ",Action::Entry,0,0,0,240,41);
        auto t=lv_obj_get_child(b,0);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_LEFT,0);
        lv_obj_align(t,LV_ALIGN_LEFT_MID,6,0);lv_obj_set_width(t,222);
    }
    updateRows();lv_obj_update_layout(m_body);lv_obj_scroll_to_y(m_body,m_offset,LV_ANIM_OFF);updateRows();
}
void Programming::drawReader() {
    if(m_blocks.size()!=m_article.sections.size())return;
    if(m_full){lv_obj_set_hidden(m_header,true);lv_obj_set_hidden(m_footer,true);}
    lv_obj_set_scrollable(m_body,true);m_positions.clear();
    const auto font=m_state.large?ui::theme::fontCjkFull():ui::theme::fontReading();
    int y=6;
    for(unsigned i=0;i<m_article.sections.size();++i) {
        const auto &s=m_article.sections[i];if(bool(s.kind)!=m_code)continue;
        auto heading=label(m_body,s.title,6,y,226,true);
        lv_obj_set_style_text_color(heading,ui::theme::accent(),0);lv_obj_update_layout(heading);
        const int top=y;y+=lv_obj_get_height(heading)+6;
        const int contentY=y;int height=0;
        if(s.kind) {
            std::string source=ui::code::unindent(m_blocks[i],m_state.wrap?0:ui::code::commonIndent(m_blocks[i]));
            std::string numbers;unsigned line=s.firstLine?s.firstLine:1;
            if(m_state.wrap) {
                std::string wrapped;
                for(size_t at=0;at<source.size();) {
                    auto end=source.find('\n',at);if(end==std::string::npos)end=source.size();
                    bool continuation=false;
                    do {
                        if(!numbers.empty()){numbers+='\n';wrapped+='\n';}
                        numbers+=continuation?">":std::to_string(line);
                        auto n=std::min(ui::code::WrapColumns,end-at);wrapped.append(source,at,n);at+=n;continuation=true;
                    }while(at<end);
                    at=end+1;++line;
                }
                source=std::move(wrapped);
            }else numbers=ui::code::lineNumbers(source,line);
            height=int(1+std::count(source.begin(),source.end(),'\n'))*ui::code::RowHeight+ui::code::Top+ui::code::Scrollbar;
            auto container=ui::compact::panel(m_body,240,height);lv_obj_set_pos(container,0,y);
            lv_obj_set_event_bubble(container,true);
            auto view=ui::code::create(container,240,height,source,numbers,m_state.wrap?LV_DIR_NONE:LV_DIR_HOR);
            lv_obj_set_scroll_chain_ver(view.viewport,true);
            lv_obj_set_event_bubble(view.viewport,true);
            lv_obj_add_event_cb(view.viewport,[](lv_event_t *e){
                auto viewport=lv_event_get_target_obj(e);
                const int x=lv_obj_get_scroll_x(viewport),right=lv_obj_get_scroll_right(viewport);
                if(x<0 || right<0)lv_obj_scroll_to_x(viewport,std::max(0,x+std::min(0,right)),LV_ANIM_OFF);
            },LV_EVENT_SCROLL_END,nullptr);
            if(!m_codeViewport) {
                m_codeViewport=view.viewport;lv_obj_update_layout(view.viewport);lv_obj_scroll_to_x(view.viewport,m_codeX,LV_ANIM_OFF);
                lv_obj_add_event_cb(view.viewport,[](lv_event_t *e){auto self=static_cast<Programming *>(lv_event_get_user_data(e));if(!self->m_drawing)self->m_codeX=std::max(0,int(lv_obj_get_scroll_x(lv_event_get_target_obj(e))));},LV_EVENT_SCROLL,this);
            }
        }else {
            auto text=label(m_body,m_blocks[i],6,y,226,true);lv_obj_set_style_text_font(text,font,0);
            lv_obj_set_style_text_line_space(text,3,0);lv_obj_update_layout(text);height=lv_obj_get_height(text);
        }
        m_positions.push_back({i,top,contentY,height});y+=height+12;
    }
    // All content is owned by this App; only one selected article is materialized.
    lv_obj_update_layout(m_body);
    int scroll=m_scroll[m_code];
    if(m_pendingRestore) {
        for(const auto &p:m_positions)if(p.section==m_section) {
            scroll=p.y;
            if(m_restore && !m_blocks[p.section].empty())scroll=p.content+int(std::min(m_restore,m_blocks[p.section].size())*size_t(p.height)/m_blocks[p.section].size());
        }
        m_pendingRestore=false;m_restore=0;
    }
    lv_obj_scroll_to_y(m_body,scroll,LV_ANIM_OFF);m_scroll[m_code]=std::max(0,int(lv_obj_get_scroll_y(m_body)));
}
void Programming::mark() {
    if(!m_library || m_positions.empty())return;
    const int y=std::max(0,int(lv_obj_get_scroll_y(m_body)));
    const Position *current=&m_positions.front();for(const auto &p:m_positions)if(p.y<=y)current=&p;
    m_section=current->section;
    size_t offset=current->height>0?size_t(std::max(0,y-current->content))*m_blocks[m_section].size()/current->height:0;
    offset=std::min(offset,m_blocks[m_section].size());
    while(offset && offset<m_blocks[m_section].size() && (uint8_t(m_blocks[m_section][offset])&0xc0)==0x80)--offset;
    m_state.visit({m_library->entry(m_entry).id,m_article.sections[m_section].id,uint32_t(offset)});dirty();
}
void Programming::turn(int delta) {
    if(m_job || m_waitOpen || ui::TextInput::visible())return;
    lv_obj_scroll_by_bounded(m_body,0,-delta*120,LV_ANIM_OFF);
    if(m_page==Page::Reader)mark();
}
bool Programming::onBack() {
    if(ui::TextInput::visible()){ui::TextInput::hide();return true;}
    if(m_job){m_job->cancelled.store(true);m_job.reset();}m_waitOpen=false;
    if(m_page==Page::Busy && m_library){releaseView();m_page=m_origin;m_offset=m_listOffset;draw();return true;}
    if(m_page==Page::Home)return false;
    if(m_page==Page::Reader){mark();releaseView();m_full=false;m_page=m_origin;m_offset=m_listOffset;draw();return true;}
    if(m_page==Page::Related){m_page=Page::Reader;m_offset=0;draw();return true;}
    if(m_page==Page::Options || m_page==Page::Info){m_page=m_return;draw();return true;}
    if(m_page==Page::List && m_mode==Mode::Group){m_page=Page::Groups;m_offset=0;groups();}
    else {releaseView();m_page=Page::Home;}
    if(!m_library){m_page=Page::Home;return false;}draw();return true;
}
void Programming::click(lv_event_t *e) {
    auto self=static_cast<Programming *>(lv_event_get_user_data(e));
    if(auto indev=lv_indev_active()) {
        lv_point_t point{};lv_indev_get_point(indev,&point);
        if(std::abs(point.x-self->m_press.x)+std::abs(point.y-self->m_press.y)>12)return;
    }
    auto value=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e))));
    self->act(Action(value&255),unsigned(value>>8));
}
void Programming::act(Action a,unsigned value) {
    if(a==Action::Back){if(!onBack())sys::EventBus::publish({sys::EventType::BackRequested});return;}
    if(a==Action::Reload){load();return;}if(!m_library || m_job || m_waitOpen)return;
    switch(a) {
    case Action::Search:m_page=Page::Search;releaseView();break;
    case Action::Menu:if(m_page==Page::Reader)mark();if(m_page!=Page::Options && m_page!=Page::Info)m_return=m_page;m_page=Page::Options;break;
    case Action::Category:m_category=value;m_page=Page::Groups;m_offset=0;groups();break;
    case Action::Group:if(value>=m_groups.size())return;m_group=m_groups[value];m_mode=Mode::Group;m_page=Page::List;m_offset=0;filter();break;
    case Action::Favorites:case Action::Recent:m_mode=a==Action::Favorites?Mode::Favorites:Mode::Recent;m_page=Page::List;m_offset=0;filter();break;
    case Action::Resume:if(!m_state.recent.empty()){auto mark=m_state.recent.front();auto i=m_library->find(mark.id);if(i>=0){m_origin=Page::Home;m_listOffset=0;request(i,0,mark.offset,mark.section);return;}}return;
    case Action::Entry:{if(m_page==Page::List){m_origin=Page::List;m_listOffset=m_offset;}auto e=m_library->entry(value);auto mark=std::find_if(m_state.recent.begin(),m_state.recent.end(),[e](const auto &m){return m.id==e.id;});request(value,0,mark==m_state.recent.end()?0:mark->offset,mark==m_state.recent.end()?0:mark->section);return;}
    case Action::Submit:m_query=m_input?lv_textarea_get_text(m_input):"";m_mode=Mode::Search;m_page=Page::List;m_offset=0;filter();break;
    case Action::Home:releaseView();m_full=false;m_page=Page::Home;break;
    case Action::Favorite:if(m_state.toggle(m_library->entry(m_entry).id))dirty();else m_message="收藏数量已达上限。";break;
    case Action::Toggle:if(!m_hasCode)return;mark();m_code=!m_code;break;
    case Action::Related:if(m_article.sections.empty())return;m_page=Page::Related;m_offset=0;break;
    case Action::Wrap:m_state.wrap=!m_state.wrap;m_codeX=0;dirty();m_scroll[0]=m_scroll[1]=0;break;
    case Action::Size:m_state.large=!m_state.large;dirty();m_scroll[0]=m_scroll[1]=0;break;
    case Action::Full:if(m_blocks.empty())return;m_full=true;m_page=Page::Reader;if(m_page==Page::Reader && !m_power)m_power=sys::Buttons::claimViewExit();break;
    case Action::Info:m_page=Page::Info;break;
    default:return;
    }draw();
}
}
