#include "file_manager.hpp"
#include "core/charset_conv.hpp"
#include "core/event_bus.hpp"
#include "core/storage_browser.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
namespace apps {
namespace {
std::string base(const std::string &path){return path.substr(path.find_last_of('/')+1);}
lv_obj_t *text(lv_obj_t *parent,const std::string &value,int width=236) {
    auto label=ui::compact::label(parent,core::ensureUtf8(value).c_str(),width);
    lv_obj_set_style_text_font(label,ui::theme::fontCjkFull(),0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);return label;
}
lv_obj_t *row(lv_obj_t *parent,int height=36) {
    auto obj=ui::compact::panel(parent,240,height);
    lv_obj_set_flex_flow(obj,LV_FLEX_FLOW_ROW);return obj;
}
}
lv_obj_t *FileManager::button(lv_obj_t *parent,const char *caption,Action act,int width,int height) {
    auto obj=ui::compact::button(parent,caption,click,this,width,height);
    lv_obj_set_user_data(obj,reinterpret_cast<void *>(act));return obj;
}
void FileManager::onCreate(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent,ui::theme::bg(),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    lv_obj_set_scrollable(parent,false);
    auto header=ui::compact::panel(parent,240,36);lv_obj_set_pos(header,0,0);
    button(header,LV_SYMBOL_LEFT,Action::Back,36,36);
    m_title=text(header,"文件管理",166);lv_label_set_long_mode(m_title,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(m_title,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(m_title,LV_ALIGN_LEFT_MID,37,0);
    auto refresh=button(header,LV_SYMBOL_REFRESH,Action::Refresh,36,36);lv_obj_align(refresh,LV_ALIGN_RIGHT_MID,0,0);
    m_body=ui::compact::panel(parent,240,203,true);lv_obj_set_pos(m_body,0,37);
    lv_obj_set_flex_flow(m_body,LV_FLEX_FLOW_COLUMN);
    m_timer=lv_timer_create([](lv_timer_t *t){static_cast<FileManager *>(lv_timer_get_user_data(t))->poll();},100,this);
    lv_timer_pause(m_timer);
}
void FileManager::onShow(){m_hidden=false;scan();}
void FileManager::onHide() {
    m_hidden=true;ui::TextInput::hide();
    if(m_job){m_job->cancelled.store(true);m_job.reset();}
    if(m_timer)lv_timer_pause(m_timer);
    m_clipboard.clear();m_entries.clear();m_generation=m_clipGeneration=0;m_properties={};
}
void FileManager::onDestroy(){onHide();if(m_timer){lv_timer_delete(m_timer);m_timer=nullptr;}}
bool FileManager::onBack() {
    if(m_job){m_job->cancelled.store(true);return true;}
    if(m_page==Page::Properties){m_page=Page::Actions;m_properties={};m_message.clear();draw();return true;}
    if(m_page!=Page::List){m_page=Page::List;draw();return true;}
    if(m_directory!="/sdcard"){m_directory=core::parentDirectory(m_directory);m_offset=0;scan();return true;}
    return false;
}
void FileManager::scan(const core::files::Entry *anchor,bool before) {
    if(m_job)return;
    core::files::Request r;r.source=m_directory;r.offset=m_offset;
    if(anchor){r.anchor=anchor->name;r.anchorDirectory=anchor->directory;r.before=before;}
    m_page=Page::List;m_entries.clear();m_more=false;
    launch(std::move(r));
}
void FileManager::launch(core::files::Request request) {
    if(m_job)return;
    ui::TextInput::hide();
    m_job=core::files::start(std::move(request),m_generation);
    if(!m_job){m_message="存储任务忙，请稍后刷新";m_page=Page::List;draw();return;}
    m_page=Page::Busy;draw();lv_timer_resume(m_timer);
}
void FileManager::poll() {
    if(!m_job || m_hidden)return;
    if(!m_job->done.load(std::memory_order_acquire)) {
        if(m_progress){char buf[64];std::snprintf(buf,sizeof(buf),"%lu KB  /  %lu 项",
            static_cast<unsigned long>(m_job->kilobytes.load()),static_cast<unsigned long>(m_job->changed.load()));
            ui::compact::setText(m_progress,buf);}
        return;
    }
    // Wait one GUI tick if the completed worker is still releasing its owner.
    if(!core::files::idle())return;
    auto job=std::move(m_job);lv_timer_pause(m_timer);m_page=Page::List;
    if(job->request.operation==core::files::Operation::Properties) {
        m_properties=job->result.properties;
        m_message=job->result.error?core::files::errorText(job->result.error):"";
        if(job->result.error==ECANCELED)m_message="已取消属性读取";
        // A bounded scan can still show metadata and clearly marked partial totals.
        const bool partial=job->result.error==E2BIG || job->result.error==ELOOP;
        m_page=m_properties.valid && (!job->result.error || partial)?Page::Properties:Page::Actions;
        draw();return;
    }
    if(job->request.operation==core::files::Operation::List) {
        m_generation=job->result.error?0:job->generation;
        m_entries=std::move(job->result.entries);m_more=job->result.more;
        if(job->result.error)m_message=core::files::errorText(job->result.error);
        if(m_clipGeneration && m_clipGeneration!=m_generation){m_clipboard.clear();m_clipGeneration=0;}
        draw();return;
    }
    m_message=core::files::errorText(job->result.error);
    if(job->result.error==ECANCELED && job->request.operation!=core::files::Operation::Remove)m_message="已取消，原文件保留";
    if(!job->result.error && job->request.operation==core::files::Operation::Move){m_clipboard.clear();m_clipGeneration=0;}
    m_offset=0;scan();
}
void FileManager::select(size_t index,bool actions) {
    if(m_job || index>=m_entries.size())return;
    const auto &entry=m_entries[index];
    m_selected=m_directory+"/"+entry.name;m_message.clear();
    if(entry.directory && !actions){m_directory=m_selected;m_offset=0;scan();}
    else {m_page=Page::Actions;draw();}
}
void FileManager::draw() {
    ui::TextInput::hide();m_input=m_progress=nullptr;lv_obj_clean(m_body);lv_obj_scroll_to_y(m_body,0,LV_ANIM_OFF);
    const auto title=m_page==Page::List?(m_directory=="/sdcard"?std::string("SD 卡"):base(m_directory)):
        m_page==Page::Actions?"文件操作":m_page==Page::Name?"名称":m_page==Page::Delete?"确认删除":m_page==Page::Properties?"属性":"正在处理";
    lv_label_set_text(m_title,core::ensureUtf8(title).c_str());
    if(m_page==Page::Busy) {
        text(m_body,m_job && m_job->request.operation==core::files::Operation::List?"正在读取目录…":
            m_job && m_job->request.operation==core::files::Operation::Properties?"正在统计属性，可取消":"正在操作，请勿拔出SD卡");
        m_progress=text(m_body,"0 KB / 0 项");button(m_body,"取消",Action::Cancel,240,40);return;
    }
    if(m_page==Page::Properties) {
        auto field=[&](const char *caption,const std::string &value) {
            auto heading=text(m_body,caption);lv_obj_set_style_text_color(heading,ui::theme::muted(),0);
            text(m_body,value);
        };
        field("名称",base(m_selected));
        field("类型",m_properties.directory?"文件夹":"文件");
        char size[80];std::snprintf(size,sizeof(size),"%llu 字节",static_cast<unsigned long long>(m_properties.bytes));
        field(m_properties.complete?"大小":"已统计大小",size);
        if(m_properties.directory) {
            char count[80];std::snprintf(count,sizeof(count),"%u 个文件 / %u 个文件夹",m_properties.files,m_properties.folders);
            field("包含（含子目录）",count);
        }
        const time_t stamp=static_cast<time_t>(m_properties.modified);tm local{};
        char date[32]="--";
        if(stamp>0 && localtime_r(&stamp,&local))std::strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",&local);
        field("修改时间",date);field("完整路径",m_selected);
        if(!m_properties.complete)text(m_body,"目录过深或项目过多，仅显示已统计部分。");
        return;
    }
    if(m_page==Page::Name) {
        text(m_body,m_new?"新建文件夹":"重命名");
        m_input=ui::compact::networkField(m_body,"名称",m_new?"":base(m_selected).c_str(),80,ui::theme::fontCjkFull());
        auto buttons=row(m_body);button(buttons,"取消",Action::Cancel,119,36);button(buttons,"确定",Action::Confirm,120,36);
        if(!m_message.empty())text(m_body,m_message);
        return;
    }
    if(m_page==Page::Delete) {
        text(m_body,base(m_selected));text(m_body,"删除此项目及其全部内容？无法恢复。");
        auto buttons=row(m_body,40);button(buttons,"取消",Action::Cancel,119,40);
        auto del=button(buttons,"删除",Action::Confirm,120,40);ui::compact::buttonBackground(del,ui::theme::danger());return;
    }
    if(m_page==Page::Actions) {
        text(m_body,base(m_selected));
        auto first=row(m_body,40);button(first,"复制",Action::Copy,119,40);button(first,"移动",Action::Move,120,40);
        auto second=row(m_body,40);button(second,"重命名",Action::Rename,119,40);button(second,"删除",Action::Remove,120,40);
        button(m_body,"属性",Action::Properties,240,40);
        if(!m_message.empty())text(m_body,m_message);
        text(m_body,"复制或移动后，进入目标目录点击粘贴。");return;
    }
    auto tools=row(m_body);
    auto create=button(tools,LV_SYMBOL_PLUS " 文件夹",Action::New,119,36);
    auto paste=button(tools,"粘贴",Action::Paste,120,36);
    ui::compact::disabled(create,!m_generation);ui::compact::disabled(paste,m_clipboard.empty() || !m_generation);
    if(!m_clipboard.empty()) {
        auto clip=row(m_body);auto name=text(clip,(m_move?"移动：":"复制：")+base(m_clipboard),198);
        lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_DOTS);lv_obj_align(name,LV_ALIGN_LEFT_MID,1,0);
        auto clear=button(clip,LV_SYMBOL_CLOSE,Action::Clear,36,36);lv_obj_align(clear,LV_ALIGN_RIGHT_MID,0,0);
        // This row uses fixed positions, avoiding a long clipboard name pushing
        // the cancel touch target off the screen.
        lv_obj_set_flex_flow(clip,LV_FLEX_FLOW_ROW);lv_obj_set_flex_grow(name,1);
    }
    if(!m_message.empty()){auto note=text(m_body,m_message);lv_obj_set_style_text_color(note,ui::theme::muted(),0);}
    if(m_entries.empty() && m_generation)text(m_body,"此目录为空");
    for(size_t i=0;i<m_entries.size();++i) {
        const auto &e=m_entries[i];auto line=row(m_body,40);
        auto item=ui::compact::button(line,"",nullptr,nullptr,201,40);
        lv_obj_set_user_data(item,reinterpret_cast<void *>(i));
        lv_obj_add_event_cb(item,[](lv_event_t *event) {
            auto self=static_cast<FileManager *>(lv_event_get_user_data(event));
            self->select(reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))),false);
        },LV_EVENT_CLICKED,this);
        auto icon=ui::compact::label(item,e.directory?LV_SYMBOL_DIRECTORY:LV_SYMBOL_FILE,20);lv_obj_align(icon,LV_ALIGN_LEFT_MID,4,0);
        auto name=text(item,e.name,166);lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_DOTS);lv_obj_align(name,LV_ALIGN_LEFT_MID,28,0);
        auto more=ui::compact::button(line,LV_SYMBOL_LIST,nullptr,nullptr,38,40);
        lv_obj_set_user_data(more,reinterpret_cast<void *>(i));
        lv_obj_add_event_cb(more,[](lv_event_t *event) {
            auto self=static_cast<FileManager *>(lv_event_get_user_data(event));
            self->select(reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))),true);
        },LV_EVENT_CLICKED,this);
    }
    if(m_offset || m_more) {
        auto pages=row(m_body);auto previous=button(pages,LV_SYMBOL_LEFT,Action::Previous,58,36);
        char page[32];std::snprintf(page,sizeof(page),"%u",unsigned(m_offset/core::files::PageSize+1));
        auto number=text(pages,page,120);lv_obj_set_style_text_align(number,LV_TEXT_ALIGN_CENTER,0);
        auto next=button(pages,LV_SYMBOL_RIGHT,Action::Next,59,36);
        ui::compact::disabled(previous,m_offset==0);ui::compact::disabled(next,!m_more);
    }
}
void FileManager::click(lv_event_t *event) {
    auto self=static_cast<FileManager *>(lv_event_get_user_data(event));
    self->action(static_cast<Action>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)))));
}
void FileManager::action(Action action) {
    using Op=core::files::Operation;
    if(action==Action::Back){if(!onBack())sys::EventBus::publish({sys::EventType::HomeRequested});return;}
    if(m_job){if(action==Action::Cancel)m_job->cancelled.store(true);return;}
    if(action==Action::Properties || (action==Action::Refresh && m_page==Page::Properties)) {
        if(!m_generation || m_selected.empty())return;
        m_properties={};m_message.clear();launch({Op::Properties,m_selected,{}});return;
    }
    if(action==Action::Refresh){m_message.clear();scan();return;}
    if(action==Action::Previous || action==Action::Next) {
        m_offset=action==Action::Next?m_offset+core::files::PageSize:m_offset>=core::files::PageSize?m_offset-core::files::PageSize:0;
        m_message.clear();scan(m_entries.empty()?nullptr:action==Action::Next?&m_entries.back():&m_entries.front(),action==Action::Previous);return;
    }
    if(action==Action::New || action==Action::Rename) {m_new=action==Action::New;m_page=Page::Name;m_message.clear();draw();return;}
    if(action==Action::Copy || action==Action::Move) {
        m_clipboard=m_selected;m_clipGeneration=m_generation;m_move=action==Action::Move;
        m_page=Page::List;m_message="进入目标目录后点击粘贴";draw();return;
    }
    if(action==Action::Clear){m_clipboard.clear();m_clipGeneration=0;m_message.clear();draw();return;}
    if(action==Action::Paste) {
        if(m_clipboard.empty() || !m_generation || m_clipGeneration!=m_generation)return;
        launch({m_move?Op::Move:Op::Copy,m_clipboard,m_directory+"/"+base(m_clipboard)});return;
    }
    if(action==Action::Remove){m_page=Page::Delete;draw();return;}
    if(action==Action::Cancel){m_page=Page::List;m_message.clear();draw();return;}
    if(action==Action::Confirm && m_page==Page::Delete){launch({Op::Remove,m_selected,{}});return;}
    if(action==Action::Confirm && m_page==Page::Name && m_input) {
        const std::string name=lv_textarea_get_text(m_input);
        if(!core::files::validName(name)){m_message="名称为空、过长或包含无效字符";draw();return;}
        const auto target=m_directory+"/"+name;
        launch(m_new?core::files::Request{Op::Mkdir,target,{}}:core::files::Request{Op::Rename,m_selected,target});
    }
}
}
