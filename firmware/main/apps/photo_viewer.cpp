#include "photo_viewer.hpp"
#include "core/screenshot.hpp"

#include "charset_conv.hpp"
#include "theme.hpp"
#include "compact_ui.hpp"
#include "button_mgr.hpp"
#include "event_bus.hpp"
#include "power_mgr.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace apps {
namespace {

constexpr int32_t kViewW = 240;      // image panel width  (fills screen edge-to-edge)
constexpr int32_t kViewH = 240;      // full-screen image; list retains its own header
constexpr int32_t kScaleStepNum = 3; // zoom step ratio = 3/2 (+50% per tap)
constexpr int32_t kScaleStepDen = 2;


} // namespace

void PhotoViewer::onCreate(lv_obj_t *parent)
{
    lv_obj_set_style_text_font(parent,ui::theme::fontCjkFull(),0);
    m_poll=lv_timer_create([](lv_timer_t *t) {
        auto self=static_cast<PhotoViewer *>(lv_timer_get_user_data(t));
        if(sys::Buttons::takeViewExit(self->m_powerSession)) {self->showList();return;}
        self->pollStorage();self->pollImage();self->pollGif();
    },100,this);
    lv_timer_pause(m_poll);
    m_previousSub=sys::EventBus::subscribe(sys::EventType::PreviousPage,[this](const sys::Event &event) {
        if(m_keySession && event.i32==static_cast<int32_t>(m_keySession)) {if(m_gifMode)switchGif(-1);else zoom(-1);}
    });
    m_nextSub=sys::EventBus::subscribe(sys::EventType::NextPage,[this](const sys::Event &event) {
        if(m_keySession && event.i32==static_cast<int32_t>(m_keySession)) {if(m_gifMode)switchGif(1);else zoom(1);}
    });
    lv_obj_set_style_bg_color(parent, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(parent,false);
    lv_obj_add_event_cb(parent,[](lv_event_t *event) {
        auto self=static_cast<PhotoViewer *>(lv_event_get_user_data(event));
        auto indev=lv_indev_active(); if(!indev || self->m_viewing) return;
        const auto dir=lv_indev_get_gesture_dir(indev);
        if(dir==LV_DIR_LEFT || dir==LV_DIR_RIGHT) {
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
    lv_obj_set_width(m_title,166);
    lv_label_set_long_mode(m_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m_title, ui::theme::fontSmall(), 0);
    lv_obj_set_style_text_color(m_title, ui::theme::fg(), 0);
    lv_obj_align(m_title,LV_ALIGN_LEFT_MID,37,0);

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
        auto self=static_cast<PhotoViewer *>(context);
        return (self->m_directories[index]?std::string("[\u76ee\u5f55] "):std::string())+self->m_names[index];
    },[](void *context,size_t index) {static_cast<PhotoViewer *>(context)->openFile(index);});

    m_imagePanel = lv_obj_create(parent);
    lv_obj_set_size(m_imagePanel, kViewW, kViewH);
    lv_obj_align(m_imagePanel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(m_imagePanel, 0, 0);
    lv_obj_set_style_border_width(m_imagePanel, 0, 0);
    lv_obj_set_style_radius(m_imagePanel, 0, 0);
    lv_obj_set_style_bg_color(m_imagePanel, lv_color_black(), 0);
    lv_obj_set_scrollable(m_imagePanel,false);
    lv_obj_set_hidden(m_imagePanel,true);

    m_image = lv_image_create(m_imagePanel);
    lv_obj_set_clickable(m_image,false);
    lv_obj_set_clickable(m_imagePanel,true);
    lv_obj_add_event_cb(m_imagePanel, imagePressCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(m_imagePanel, imagePressingCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(m_imagePanel, imageReleaseCb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(m_imagePanel, imageReleaseCb, LV_EVENT_PRESS_LOST, this);
    lv_obj_set_pos(m_image, 0, 0);
    m_notice=lv_label_create(m_imagePanel);
    lv_obj_set_width(m_notice,232);
    lv_obj_set_style_text_font(m_notice,ui::theme::fontCjkFull(),0);
    lv_obj_set_style_text_align(m_notice,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(m_notice,lv_color_white(),0);
    lv_obj_set_style_bg_color(m_notice,lv_color_black(),0);
    lv_obj_set_style_bg_opa(m_notice,LV_OPA_80,0);
    lv_label_set_long_mode(m_notice,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(m_notice);
    lv_obj_set_hidden(m_notice,true);
}

void PhotoViewer::onShow(void)
{
    m_keySession=sys::Buttons::claimPageKeys();
    scanFiles();
}
void PhotoViewer::scanFiles(void)
{
    if(m_job) m_job->cancelled.store(true);
    m_fileList.reset();
    m_paths.clear(); m_names.clear(); m_directories.clear(); m_scanError.clear();
    m_listingTruncated=false; m_viewing=false;
    m_job=core::listDirectory(m_directory,true);
    if(!m_job) {m_scanError=core::storageError(ENOMEM);showList();return;}
    if(m_poll) lv_timer_resume(m_poll);
    showList(); showMessage("正在读取目录…");
}
void PhotoViewer::pollStorage()
{
    if(!m_job || !m_job->done.load(std::memory_order_acquire)) return;
    auto job=std::move(m_job);
    if(m_poll) lv_timer_pause(m_poll);
    if(job->cancelled.load()) return;
    if(job->error) m_scanError=core::storageError(job->error);
    m_listingTruncated=job->truncated;
    for(auto &entry:job->entries) {
        m_paths.push_back(std::move(entry.path)); m_names.push_back(std::move(entry.name));
        m_directories.push_back(entry.directory);
    }
    showList();
}
bool PhotoViewer::onBack()
{
    if(m_viewing) { showList(); return true; }
    if(m_directory!="/sdcard") { m_directory=core::parentDirectory(m_directory); scanFiles(); return true; }
    return false;
}
void PhotoViewer::onDestroy(void)
{
    onHide();
    m_fileList.detach();
    sys::EventBus::unsubscribe(m_previousSub);m_previousSub=0;
    sys::EventBus::unsubscribe(m_nextSub);m_nextSub=0;
    if(m_poll) { lv_timer_delete(m_poll); m_poll=nullptr; }
    releaseImage();
    releaseSource();
}
void PhotoViewer::onHide(void)
{
    releaseGif();
    sys::Screenshot::setViewBlocked(false);
    sys::Buttons::releasePageKeys(m_keySession);m_keySession=0;
    sys::Buttons::releaseViewExit(m_powerSession);m_powerSession=0;
    releaseSource();
    if(m_job) { m_job->cancelled.store(true); m_job.reset(); }
    if(m_poll) lv_timer_pause(m_poll);
}
void PhotoViewer::showList(void)
{
    releaseGif();
    releaseImage();
    releaseSource();
    m_viewing=false;
    sys::Screenshot::setViewBlocked(false);
    if(m_poll && !m_job) lv_timer_pause(m_poll);
    sys::Buttons::releaseViewExit(m_powerSession);m_powerSession=0;
    lv_obj_set_hidden(m_header,false);
    lv_obj_set_hidden(m_notice,true);m_noticeUntil=0;
    lv_obj_set_hidden(m_list,false);
    lv_obj_set_hidden(m_imagePanel,true);
    lv_obj_set_hidden(m_back,false);
    lv_obj_set_hidden(m_refresh,false);
    lv_obj_set_width(m_title,166);
    lv_obj_set_style_text_font(m_title,ui::theme::fontSmall(),0);
    lv_label_set_text(m_title,core::ensureUtf8(m_directory.substr(7).empty()?"SD 卡":m_directory.substr(7)).c_str());
    if(!m_scanError.empty()) { showMessage(m_scanError.c_str()); return; }
    if (m_names.empty()) {
        showMessage("此目录没有子目录或 JPEG/PNG/GIF 图片");
        return;
    }
    m_fileList.show(m_names.size(),m_listingTruncated?"目录较大，仅展示前 256 项，请进入子目录":nullptr);
}

void PhotoViewer::openFile(size_t index)
{
    if(index>=m_paths.size() || m_imageJob) return;
    if(m_directories[index]) {m_directory=m_paths[index];scanFiles();return;}
    if(core::gif::isFile(m_paths[index])) {openGif(index);return;}
    m_viewing=true;
    sys::Screenshot::setViewBlocked(true);
    m_powerSession=sys::Buttons::claimViewExit();
    m_offsetX=m_offsetY=0;
    m_imageJob=core::photo::start(m_paths[index]);
    lv_label_set_text(m_title,m_names[index].c_str());
    lv_obj_set_hidden(m_refresh,true);
    showMessage(m_imageJob?"正在读取图片…":"图片任务忙，请返回后重试");
    if(m_poll) {lv_timer_set_period(m_poll,40);lv_timer_resume(m_poll);}
}

void PhotoViewer::pollImage()
{
    if(m_noticeUntil && int32_t(lv_tick_get()-m_noticeUntil)>=0) {
        lv_obj_set_hidden(m_notice,true);m_noticeUntil=0;
    }
    if(!m_imageJob) {submitView();return;}
    if(!m_imageJob->done.load(std::memory_order_acquire)) return;
    auto job=std::move(m_imageJob);
    if(m_pendingView && (job->error || !job->pixels || job->cancelled.load() || job->view.scaled_width!=m_requestedW)) {
        submitView();return;
    }
    if(!m_viewing || !m_keySession || job->cancelled.load()) return;
    if(job->error || !job->pixels) {
        const char *error=job->error?job->error:"图片读取已取消";
        if(!m_pixels) {
            lv_obj_set_hidden(m_header,false);
            lv_obj_set_hidden(m_list,false);
            lv_obj_set_hidden(m_imagePanel,true);
            showMessage(error);
        } else {
            // A failed zoom keeps the current image and briefly shows its error.
            lv_label_set_text(m_notice,error);lv_obj_center(m_notice);
            lv_obj_set_hidden(m_notice,false);m_noticeUntil=lv_tick_get()+1800;
            m_requestedW=m_dispW;m_offsetX=m_renderX;m_offsetY=m_renderY;placeImage();
        }
        return;
    }
    releaseImage();m_pixels=job->pixels;job->pixels=nullptr;
    m_source=job->source;m_origW=m_source->width;m_origH=m_source->height;
    m_fitW=job->fitWidth;m_dispW=job->view.scaled_width;
    m_imgW=job->width;m_imgH=job->height;
    m_renderX=job->view.x;m_renderY=job->view.y;
    if(!m_pendingView){m_requestedW=m_dispW;m_offsetX=m_renderX;m_offsetY=m_renderY;}
    m_draggable=job->view.scaled_width>240 || job->view.scaled_height>240;
    m_imageDsc={};
    m_imageDsc.header.magic=LV_IMAGE_HEADER_MAGIC;
    m_imageDsc.header.cf=LV_COLOR_FORMAT_RGB565;
    m_imageDsc.header.w=m_imgW;m_imageDsc.header.h=m_imgH;
    m_imageDsc.header.stride=m_imgW*2;
    m_imageDsc.data_size=size_t(m_imgW)*m_imgH*2;m_imageDsc.data=m_pixels;
    lv_image_set_src(m_image,&m_imageDsc);
    lv_obj_set_hidden(m_header,true);
    lv_obj_set_hidden(m_list,true);
    lv_obj_set_hidden(m_imagePanel,false);
    lv_obj_set_hidden(m_notice,true);m_noticeUntil=0;
    placeImage();
    submitView();
}

void PhotoViewer::releaseImage(void)
{
    if(m_pixels) {
        lv_image_cache_drop(&m_imageDsc);
        if(m_image) lv_image_set_src(m_image,nullptr);
        heap_caps_free(m_pixels); m_pixels=nullptr;
    }
    m_imageDsc = {};
}

void PhotoViewer::releaseSource(void)
{
    if(m_imageJob) {m_imageJob->cancelled.store(true);m_imageJob.reset();}
    m_source.reset();
    m_pendingView=m_dragging=false;m_requestedW=0;m_requestAt=0;m_draggable=false;
    m_origW=m_origH=0;
}

void PhotoViewer::releaseGif()
{
    // Detach before releasing either frame, including a worker that is still
    // decoding. The worker has its own shared owner and will close the SD FILE.
    if(m_gifJob) {
        lv_image_cache_drop(&m_imageDsc);
        if(m_image)lv_image_set_src(m_image,nullptr);
        m_imageDsc={};m_gifJob->cancelled.store(true);m_gifJob.reset();
    }
    m_gifMode=m_gifPending=m_gifFailed=false;m_draggable=false;
    if(m_poll)lv_timer_set_period(m_poll,100);
}
void PhotoViewer::openGif(size_t index)
{
    releaseGif();releaseImage();releaseSource();
    m_gifMode=m_viewing=m_gifPending=true;m_gifIndex=index;m_gifWait=lv_tick_get();
    sys::Screenshot::setViewBlocked(true);
    if(!m_powerSession)m_powerSession=sys::Buttons::claimViewExit();
    lv_obj_set_hidden(m_header,true);lv_obj_set_hidden(m_list,true);
    lv_obj_set_hidden(m_imagePanel,false);
    lv_label_set_text(m_notice,"正在读取 GIF…");lv_obj_center(m_notice);
    lv_obj_set_hidden(m_notice,false);m_noticeUntil=0;
    lv_timer_set_period(m_poll,10);lv_timer_resume(m_poll);
}
void PhotoViewer::switchGif(int direction)
{
    if(!m_gifMode || m_paths.empty())return;
    size_t index=m_gifIndex;
    for(size_t n=0;n<m_paths.size();++n) {
        index=direction>0?(index+1)%m_paths.size():(index+m_paths.size()-1)%m_paths.size();
        if(!m_directories[index] && core::gif::isFile(m_paths[index])) {openGif(index);return;}
    }
}
void PhotoViewer::pollGif()
{
    if(!m_gifMode || m_gifFailed)return;
    if(m_gifPending && core::gif::idle() && core::photo::idle()) {
        m_gifJob=core::gif::start(m_paths[m_gifIndex]);m_gifPending=false;
        if(!m_gifJob)failGif("GIF任务启动失败，请返回重试");
    } else if(m_gifPending && uint32_t(lv_tick_get()-m_gifWait)>5000) {
        m_gifPending=false;failGif("图片任务忙，请返回重试");
    }
    if(!m_gifJob)return;
    auto &job=*m_gifJob;
    const uint32_t now=lv_tick_get();
    const int frame=job.readyFrame(now);
    if(frame>=0) {
        lv_image_cache_drop(&m_imageDsc);
        m_imageDsc={};m_imageDsc.header.magic=LV_IMAGE_HEADER_MAGIC;
        m_imageDsc.header.cf=LV_COLOR_FORMAT_RGB565;
        m_imageDsc.header.w=job.width;m_imageDsc.header.h=job.height;m_imageDsc.header.stride=job.width*2;
        m_imageDsc.data_size=job.width*job.height*2;
        m_imageDsc.data=reinterpret_cast<const uint8_t *>(job.pixels[frame]);
        lv_image_set_src(m_image,&m_imageDsc);lv_obj_center(m_image);lv_obj_invalidate(m_image);
        lv_obj_set_hidden(m_notice,true);
        job.present(frame,now);
    }
    if(job.done.load(std::memory_order_acquire) && job.error) {
        failGif(job.error);
    }
}
void PhotoViewer::failGif(const char *error)
{
    m_gifFailed=true;
    lv_label_set_text(m_notice,error);lv_obj_center(m_notice);lv_obj_set_hidden(m_notice,false);
    // Keep exit polling and the displayed frame's shared owner until back/next.
    if(m_poll)lv_timer_set_period(m_poll,100);
}

void PhotoViewer::showMessage(const char *message)
{
    m_fileList.message(message);
}

void PhotoViewer::backCb(lv_event_t *e)
{
    if(!static_cast<PhotoViewer *>(lv_event_get_user_data(e))->onBack())
        sys::EventBus::publish({sys::EventType::HomeRequested});
}
void PhotoViewer::refreshCb(lv_event_t *e) { auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e)); s->scanFiles(); }

// A single pending request replaces older work. Keep the last completed frame
// while the worker reads; the GUI never touches SD or waits for cancellation.
void PhotoViewer::queueView(bool pan)
{
    if(!pan || !m_pendingView)m_requestAt=lv_tick_get()+(pan?30:120);
    m_pendingView=true;
    // Continue building/reading the same scale while the finger moves. A newer
    // scale cancels old work; pending pans share its complete tile cache.
    if(m_imageJob && !pan)m_imageJob->cancelled.store(true);
}
void PhotoViewer::submitView()
{
    if(!m_pendingView || m_imageJob || !m_viewing || !m_source || !m_keySession ||
       !sys::Power::isScreenOn() || sys::Power::sleepPending() ||
       int32_t(lv_tick_get()-m_requestAt)<0 || !core::photo::idle())return;
    m_imageJob=core::photo::start({},m_source,m_requestedW,m_offsetX,m_offsetY);
    if(m_imageJob) {
        m_pendingView=false;
        if(m_requestedW!=m_dispW) {
            lv_label_set_text(m_notice,"正在加载清晰画面…");lv_obj_center(m_notice);
            lv_obj_set_hidden(m_notice,false);m_noticeUntil=0;
        }
    } else {
        m_pendingView=false;m_requestedW=m_dispW;m_offsetX=m_renderX;m_offsetY=m_renderY;placeImage();
        lv_label_set_text(m_notice,"图片任务启动失败，请重试");lv_obj_center(m_notice);
        lv_obj_set_hidden(m_notice,false);m_noticeUntil=lv_tick_get()+2500;
    }
}
void PhotoViewer::applyScale(uint16_t dispW)
{
    if(!m_source)return;
    const auto old=photo_view_make(m_origW,m_origH,m_requestedW,m_offsetX,m_offsetY);
    const auto next=photo_view_make(m_origW,m_origH,dispW,0,0);
    m_offsetX=int32_t((int64_t(m_offsetX)*2+old.width)*next.scaled_width/(2*old.scaled_width))-next.width/2;
    m_offsetY=int32_t((int64_t(m_offsetY)*2+old.height)*next.scaled_height/(2*old.scaled_height))-next.height/2;
    m_requestedW=next.scaled_width;clampOffset();queueView();
}
void PhotoViewer::placeImage(void)
{
    // The image object is never larger than 240x240. During a drag, move the
    // existing tile for immediate feedback, then replace it with the new ROI.
    const int32_t dx=m_requestedW==m_dispW?m_renderX-m_offsetX:0;
    const int32_t dy=m_requestedW==m_dispW?m_renderY-m_offsetY:0;
    lv_obj_set_pos(m_image,(kViewW-m_imgW)/2+dx,(kViewH-m_imgH)/2+dy);
}
void PhotoViewer::clampOffset(void)
{
    const auto v=photo_view_make(m_origW,m_origH,m_requestedW,m_offsetX,m_offsetY);
    m_offsetX=v.x;m_offsetY=v.y;
}
void PhotoViewer::zoom(int direction)
{
    if(!m_keySession || !m_viewing || m_job || !m_pixels || !m_source || m_gifMode ||
       !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
    uint32_t next=m_requestedW;
    if(direction>0 && next<m_origW) {
        next=std::max<uint32_t>(next*kScaleStepNum/kScaleStepDen,next+8);
        next=std::min<uint32_t>(next,m_origW);
    } else if(direction<0 && next>m_fitW) {
        // Follow the same integer ladder backwards. Multiplying by 2/3 would
        // produce e.g. 1214 after 1215 -> 1822 and miss the existing cache.
        next=m_fitW;
        for(;;) {
            const uint32_t higher=std::min<uint32_t>(m_origW,
                std::max<uint32_t>(next*kScaleStepNum/kScaleStepDen,next+8));
            if(higher>=m_requestedW || higher<=next)break;
            next=higher;
        }
    }
    if(next!=m_requestedW)applyScale(static_cast<uint16_t>(next));
}
void PhotoViewer::imagePressCb(lv_event_t *e)
{
    auto *s=static_cast<PhotoViewer *>(lv_event_get_user_data(e));
    if(!s->m_draggable || s->m_gifMode || s->m_requestedW!=s->m_dispW)return;
    auto indev=lv_indev_active();if(!indev)return;
    s->m_dragging=true;lv_indev_get_point(indev,&s->m_pressPoint);
    s->m_pressOffsetX=s->m_offsetX;s->m_pressOffsetY=s->m_offsetY;
}
void PhotoViewer::imagePressingCb(lv_event_t *e)
{
    auto *s=static_cast<PhotoViewer *>(lv_event_get_user_data(e));
    if(!s->m_dragging || !s->m_draggable || s->m_gifMode)return;
    auto indev=lv_indev_active();if(!indev)return;
    lv_point_t p;lv_indev_get_point(indev,&p);
    const auto x=s->m_offsetX,y=s->m_offsetY;
    s->m_offsetX=s->m_pressOffsetX-(p.x-s->m_pressPoint.x);
    s->m_offsetY=s->m_pressOffsetY-(p.y-s->m_pressPoint.y);s->clampOffset();
    if(x!=s->m_offsetX || y!=s->m_offsetY){s->placeImage();s->queueView(true);}
}
void PhotoViewer::imageReleaseCb(lv_event_t *e)
{
    auto *s=static_cast<PhotoViewer *>(lv_event_get_user_data(e));s->m_dragging=false;
    if(s->m_pendingView)s->m_requestAt=lv_tick_get();
}

} // namespace apps
