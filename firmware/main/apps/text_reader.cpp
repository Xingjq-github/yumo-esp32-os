#include "text_reader.hpp"

#include "charset_conv.hpp"
#include "theme.hpp"
#include "compact_ui.hpp"
#include "button_mgr.hpp"
#include "event_bus.hpp"
#include "power_mgr.hpp"
#include "text_paging.hpp"
#include "src/misc/lv_text_private.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace apps {
namespace {
constexpr uint32_t kNoticeHoldMs=1000, kNoticeFadeMs=500;
constexpr uint32_t kPollMs=100, kNoticePollMs=50;
}
void TextReader::onCreate(lv_obj_t *parent)
{
    m_parent = parent;
    lv_obj_set_style_text_font(parent,ui::theme::fontCjkFull(),0);
    m_poll=lv_timer_create([](lv_timer_t *t) {
        auto self=static_cast<TextReader *>(lv_timer_get_user_data(t));
        if(sys::Buttons::takeViewExit(self->m_powerSession)) {self->showList();return;}
        self->pollStorage();
        self->pollPageNotice();
    },kPollMs,this);
    lv_timer_pause(m_poll);
    m_previousSub=sys::EventBus::subscribe(sys::EventType::PreviousPage,[this](const sys::Event &event) {
        if(m_keySession && event.i32==static_cast<int32_t>(m_keySession)) turnPage(-1);
    });
    m_nextSub=sys::EventBus::subscribe(sys::EventType::NextPage,[this](const sys::Event &event) {
        if(m_keySession && event.i32==static_cast<int32_t>(m_keySession)) turnPage(1);
    });
    lv_obj_set_style_bg_color(parent, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(parent,false);
    // Stop LVGL's gesture-target search at this window, rather than the screen.
    lv_obj_set_gesture_bubble(parent,false);
    lv_obj_add_event_cb(parent,[](lv_event_t *event) {
        auto self=static_cast<TextReader *>(lv_event_get_user_data(event));
        auto indev=lv_indev_active(); if(!indev) return;
        const auto dir=lv_indev_get_gesture_dir(indev);
        if(self->m_viewing) {
            const bool next=dir==LV_DIR_LEFT || dir==LV_DIR_TOP;
            const bool previous=dir==LV_DIR_RIGHT || dir==LV_DIR_BOTTOM;
            if(!next && !previous) return;
            lv_indev_wait_release(indev);
            self->turnPage(next?1:-1);
        } else if(dir==LV_DIR_LEFT || dir==LV_DIR_RIGHT) {
            lv_indev_wait_release(indev);
            sys::EventBus::publish({sys::EventType::BackRequested});
        }
    },LV_EVENT_GESTURE,this);

    m_header = lv_obj_create(parent);
    lv_obj_set_size(m_header, ui::theme::kContentW, 36);
    lv_obj_align(m_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(m_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_header, 0, 0);
    lv_obj_set_style_pad_all(m_header, 0, 0);
    lv_obj_set_scrollable(m_header,false);

    m_back = ui::compact::button(m_header,LV_SYMBOL_LEFT,backCb,this,36,36);
    lv_obj_align(m_back, LV_ALIGN_LEFT_MID, 0, 0);

    m_title = lv_label_create(m_header);
    lv_obj_set_width(m_title, 166);
    lv_label_set_long_mode(m_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m_title, ui::theme::fontSmall(), 0);
    lv_obj_set_style_text_color(m_title, ui::theme::fg(), 0);
    lv_obj_align(m_title,LV_ALIGN_LEFT_MID,37,0);
    lv_label_set_text(m_title, "SD 卡");

    m_refresh = ui::compact::button(m_header,LV_SYMBOL_REFRESH,refreshCb,this,36,36);
    lv_obj_align(m_refresh, LV_ALIGN_RIGHT_MID, 0, 0);

    m_list = lv_obj_create(parent);
    lv_obj_set_size(m_list, ui::theme::kContentW, 203);
    lv_obj_align(m_list, LV_ALIGN_TOP_MID, 0, 37);
    lv_obj_set_style_bg_color(m_list, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(m_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_list, 0, 0);
    lv_obj_set_style_pad_all(m_list, 0, 0);
    lv_obj_set_style_pad_row(m_list, ui::theme::kGap, 0);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(m_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_list, LV_SCROLLBAR_MODE_OFF);
    m_fileList.attach(m_list,this,[](void *context,size_t index) {
        auto self=static_cast<TextReader *>(context);
        return (self->m_directories[index]?std::string("[\u76ee\u5f55] "):std::string())+self->m_names[index];
    },[](void *context,size_t index) {static_cast<TextReader *>(context)->openFile(index);});

    m_reader = lv_obj_create(parent);
    lv_obj_remove_style_all(m_reader);
    lv_obj_set_size(m_reader, ui::theme::kContentW, ui::theme::kScreenH);
    lv_obj_align(m_reader, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(m_reader, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(m_reader, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_reader, 0, 0);
    lv_obj_set_style_pad_all(m_reader, 0, 0);
    lv_obj_set_scrollable(m_reader,false);
    lv_obj_set_gesture_bubble(m_reader,true);
    lv_obj_set_style_radius(m_reader,0,0);
    lv_obj_set_scrollbar_mode(m_reader, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_hidden(m_reader,true);

    m_text = lv_label_create(m_reader);
    lv_obj_remove_style_all(m_text);
    lv_obj_set_width(m_text, ui::theme::kContentW);
    lv_obj_set_height(m_text, LV_SIZE_CONTENT);
    lv_obj_align(m_text, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(m_text, ui::theme::fontReading(), 0);
    lv_obj_set_style_text_align(m_text, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(m_text, ui::theme::fg(), 0);
    lv_obj_set_style_text_line_space(m_text,0,0);
    lv_obj_set_style_text_letter_space(m_text,0,0);
    lv_label_set_long_mode(m_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_gesture_bubble(m_text,true);

    m_pageNotice=lv_label_create(m_reader);
    lv_obj_set_style_max_width(m_pageNotice,ui::theme::kContentW-16,0);
    lv_obj_set_style_pad_hor(m_pageNotice,8,0);
    lv_obj_set_style_pad_ver(m_pageNotice,4,0);
    lv_obj_set_style_radius(m_pageNotice,4,0);
    lv_obj_set_style_text_align(m_pageNotice,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(m_pageNotice,lv_color_white(),0);
    lv_obj_set_style_bg_color(m_pageNotice,lv_color_black(),0);
    lv_obj_set_style_bg_opa(m_pageNotice,LV_OPA_COVER,0);
    lv_label_set_long_mode(m_pageNotice,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_clickable(m_pageNotice,false);
    lv_obj_set_scrollable(m_pageNotice,false);
    lv_obj_set_hidden(m_pageNotice,true);
    lv_obj_set_gesture_bubble(m_pageNotice,true);
}

void TextReader::onShow(void)
{
    m_keySession=sys::Buttons::claimPageKeys();
    scanFiles();
}
void TextReader::scanFiles(void)
{
    if(m_job) m_job->cancelled.store(true);
    m_fileList.reset();
    m_paths.clear(); m_names.clear(); m_directories.clear(); m_scanError.clear();
    m_listingTruncated=false; m_viewing=false;
    m_job=core::listDirectory(m_directory,false);
    if(!m_job) {m_scanError=core::storageError(ENOMEM);showList();return;}
    if(m_poll) lv_timer_resume(m_poll);
    showList(); showMessage("正在读取目录…");
}
void TextReader::pollStorage()
{
    if(m_saveJob && m_saveJob->done.load(std::memory_order_acquire)) {
        if(m_viewing && m_saveJob->path==m_openPath && m_saveJob->error)
            showPageNotice(true);
        m_saveJob.reset();
    }
    // Keep the mailbox responsive while reading, including after the notice fades.
    if(!m_job) { if(!m_viewing && !m_saveJob && m_poll) lv_timer_pause(m_poll); return; }
    if(!m_job || !m_job->done.load(std::memory_order_acquire)) return;
    auto job=std::move(m_job);
    if(!m_viewing && !m_saveJob && m_poll) lv_timer_pause(m_poll);
    if(job->cancelled.load()) return;
    if(job->text) {
        if(job->error) { showList(); showMessage(core::storageError(job->error)); return; }
        m_content=std::move(job->content);
        if(job->truncated) m_content+="\n[仅显示前 64 KB]";
        if(m_content.empty()) m_content="空文档";
        m_openPath=job->path; m_progress=job->progress; m_cardGeneration=job->cardGeneration;
        paginate();
        m_page=text_paging::restore(m_pages,m_content.size(),m_progress.offset);
        m_viewing=true; showTextPage(); return;
    }
    if(job->error) m_scanError=core::storageError(job->error);
    m_listingTruncated=job->truncated;
    for(auto &entry:job->entries) {
        m_paths.push_back(std::move(entry.path)); m_names.push_back(std::move(entry.name));
        m_directories.push_back(entry.directory);
    }
    showList();
}
bool TextReader::onBack()
{
    if(m_viewing) { showList(); return true; }
    if(m_directory!="/sdcard") { m_directory=core::parentDirectory(m_directory); scanFiles(); return true; }
    return false;
}
void TextReader::showList(void)
{
    sys::Buttons::releaseViewExit(m_powerSession);m_powerSession=0;
    hidePageNotice();
    if(m_job && m_job->text) { m_job->cancelled.store(true); m_job.reset(); }
    if(!m_job && !m_saveJob && m_poll) lv_timer_pause(m_poll);
    std::string().swap(m_content); std::vector<size_t>().swap(m_pages); m_page=0;
    m_openPath.clear();
    lv_label_set_text(m_text,"");
    m_viewing=false;
    lv_obj_set_hidden(m_header,false);
    lv_obj_set_hidden(m_list,false);
    lv_obj_set_hidden(m_reader,true);
    lv_obj_set_hidden(m_back,false);
    lv_obj_set_hidden(m_refresh,false);
    lv_obj_set_width(m_title,166);
    lv_obj_set_style_text_font(m_title,ui::theme::fontSmall(),0);
    lv_obj_set_style_text_color(m_title,ui::theme::fg(),0);
    lv_label_set_text(m_title,core::ensureUtf8(m_directory.substr(7).empty()?"SD 卡":m_directory.substr(7)).c_str());

    if(!m_scanError.empty()) { showMessage(m_scanError.c_str()); return; }
    if (m_names.empty()) {
        showMessage("此目录没有子目录或文本文件");
        return;
    }
    m_fileList.show(m_names.size(),m_listingTruncated?"目录较大，仅展示前 256 项，请进入子目录":nullptr);
}

void TextReader::openFile(size_t index)
{
    if (index >= m_paths.size()) return;
    if(m_directories[index]) { m_directory=m_paths[index]; scanFiles(); return; }
    if(m_job) m_job->cancelled.store(true);
    m_job=core::readText(m_paths[index]);
    if(!m_job) {showList();showMessage(core::storageError(ENOMEM));return;}
    if(m_poll) lv_timer_resume(m_poll);
    m_viewing=true;
    m_powerSession=sys::Buttons::claimViewExit();
    hidePageNotice();
    lv_obj_set_hidden(m_header,true);
    lv_obj_set_hidden(m_list,true);
    lv_obj_set_hidden(m_reader,false);
    lv_label_set_text(m_text,"正在读取文档…");
    lv_obj_invalidate(m_reader);
}
void TextReader::turnPage(int delta)
{
    if(!m_keySession || !m_viewing || m_job || m_pages.empty() ||
       !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
    if(delta<0 && m_page>0) --m_page;
    else if(delta>0 && m_page+1<m_pages.size()) ++m_page;
    else return;
    showTextPage();
    saveProgress();
}
void TextReader::paginate()
{
    // Measure the same viewport/font used to draw, with no toolbar or reserved
    // footer. The 15px reading font has a 20px line height: 12 complete rows.
    lv_obj_update_layout(m_reader);
    lv_obj_set_width(m_text, lv_obj_get_content_width(m_reader));
    lv_obj_update_layout(m_text);
    const auto font=lv_obj_get_style_text_font(m_text,LV_PART_MAIN);
    lv_text_attributes_t attrs; lv_text_attributes_init(&attrs);
    attrs.max_width=lv_obj_get_content_width(m_text);
    attrs.letter_space=lv_obj_get_style_text_letter_space(m_text,LV_PART_MAIN);
    attrs.line_space=lv_obj_get_style_text_line_space(m_text,LV_PART_MAIN);
    const unsigned lines=text_paging::linesPerPage(lv_obj_get_content_height(m_reader),lv_font_get_line_height(font),attrs.line_space);
    m_pages=text_paging::paginate(m_content,lines,[&](const char *text,size_t bytes) {
        return lv_text_get_next_line(text,static_cast<uint32_t>(bytes),font,nullptr,&attrs);
    });
}
void TextReader::saveProgress()
{
    m_progress.page=static_cast<uint32_t>(m_page+1);
    m_progress.offset=static_cast<uint32_t>(m_pages[m_page]);
    m_saveJob=core::saveTextProgress(m_openPath,m_progress,m_cardGeneration);
    if(m_saveJob && m_poll) lv_timer_resume(m_poll);
    else showPageNotice(true);
}
void TextReader::showTextPage()
{
    if(m_pages.empty() || m_page>=m_pages.size()) return;
    const size_t begin=m_pages[m_page];
    size_t end=m_page+1<m_pages.size()?m_pages[m_page+1]:m_content.size();
    // A terminating newline belongs to this page; omit its extra empty render line.
    if(end>begin && m_content[end-1]=='\n') --end;
    lv_label_set_text(m_text,m_content.substr(begin,end-begin).c_str());
    lv_obj_scroll_to_y(m_reader,0,LV_ANIM_OFF);
    lv_obj_set_hidden(m_list,true);
    lv_obj_set_hidden(m_reader,false);
    lv_obj_set_hidden(m_header,true);
    // Clear the previous page's unused rows as well as the changed label.
    lv_obj_invalidate(m_reader);
    showPageNotice();
}
void TextReader::showPageNotice(bool saveFailed)
{
    if(!m_viewing || m_pages.empty() || !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
    lv_obj_set_style_text_font(m_pageNotice,saveFailed?ui::theme::fontSmall():
        m_pages.size()>=1000?&lv_font_montserrat_12:&lv_font_montserrat_16,0);
    lv_label_set_text_fmt(m_pageNotice,"%u/%u%s",static_cast<unsigned>(m_page+1),
        static_cast<unsigned>(m_pages.size()),saveFailed?" 未保存":"");
    lv_obj_align(m_pageNotice,LV_ALIGN_CENTER,0,ui::theme::kScreenH/6);
    lv_obj_set_style_opa(m_pageNotice,LV_OPA_COVER,0);
    lv_obj_set_hidden(m_pageNotice,false);
    m_noticeSince=lv_tick_get();m_noticeVisible=true;
    if(m_poll) {lv_timer_set_period(m_poll,kNoticePollMs);lv_timer_resume(m_poll);}
}
void TextReader::hidePageNotice()
{
    m_noticeVisible=false;
    if(m_pageNotice) lv_obj_set_hidden(m_pageNotice,true);
    if(m_poll) lv_timer_set_period(m_poll,kPollMs);
}
void TextReader::pollPageNotice()
{
    if(!m_noticeVisible) return;
    const uint32_t elapsed=lv_tick_elaps(m_noticeSince);
    if(!m_viewing || !sys::Power::isScreenOn() || sys::Power::sleepPending() ||
       elapsed>=kNoticeHoldMs+kNoticeFadeMs) {hidePageNotice();return;}
    if(elapsed>kNoticeHoldMs) {
        const auto opacity=static_cast<lv_opa_t>(LV_OPA_COVER*(kNoticeHoldMs+kNoticeFadeMs-elapsed)/kNoticeFadeMs);
        lv_obj_set_style_opa(m_pageNotice,opacity,0);
    }
}
void TextReader::onHide()
{
    sys::Buttons::releasePageKeys(m_keySession); m_keySession=0;
    sys::Buttons::releaseViewExit(m_powerSession);m_powerSession=0;
    hidePageNotice();
    m_viewing=false;
    if(m_job) { m_job->cancelled.store(true); m_job.reset(); }
    if(m_poll) lv_timer_pause(m_poll);
    // The storage worker owns the last write and exits after flushing it.
    m_saveJob.reset();
}
void TextReader::onDestroy()
{
    onHide();
    m_fileList.detach();
    sys::EventBus::unsubscribe(m_previousSub); m_previousSub=0;
    sys::EventBus::unsubscribe(m_nextSub); m_nextSub=0;
    if(m_poll) { lv_timer_delete(m_poll); m_poll=nullptr; }
}

void TextReader::showMessage(const char *message)
{
    m_fileList.message(message);
}

void TextReader::backCb(lv_event_t *e)
{
    // At the root directory, leave through the queued system lifecycle so the
    // button callback returns before its owning app and widgets are destroyed.
    if(!static_cast<TextReader *>(lv_event_get_user_data(e))->onBack())
        sys::EventBus::publish({sys::EventType::HomeRequested});
}

void TextReader::refreshCb(lv_event_t *e)
{
    auto *self = static_cast<TextReader *>(lv_event_get_user_data(e));
    self->scanFiles();
}

} // namespace apps
