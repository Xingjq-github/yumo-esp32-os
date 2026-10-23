#include "screenshot.hpp"
#include "screenshot_png.h"
#include "display_cast_codec.h"
#include "sd_media.hpp"
#include "power_mgr.hpp"
#include "button_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace sys {
namespace {
using Notice=Screenshot::Notice;
constexpr uint32_t Caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
struct Job {
    uint8_t *pixels=nullptr;
    core::sd::Access access;
    timeval time{};
    int64_t started=0;
    // GUI-owned until writing=true. Worker sees an immutable complete frame.
    bool rows[YMCAST_HEIGHT]{};
    unsigned rowCount=0;
    bool captured=false,invalid=false,writing=false;
    std::atomic<bool> done{false};
    Notice result=Notice::Failed;
    ~Job() {if(pixels) {std::memset(pixels,0,YMCAST_BYTES);heap_caps_free(pixels);}}
};
lv_display_t *display=nullptr;
Job *job=nullptr; // GUI owns lifetime; worker publishes done after releasing SD.
std::atomic<bool> requested{false},active{false};
std::atomic<bool> viewBlocked{false};
bool eligible() {
    return Power::isScreenOn() && !Power::sleepPending() &&
        !viewBlocked.load(std::memory_order_acquire) && !Buttons::blockHomeShortcut();
}

struct Output {FILE *file;int error=0;int64_t deadline,lastYield;bool ioError=false;};
int cancelled(void *context) {
    auto &out=*static_cast<Output *>(context);
    const auto now=esp_timer_get_time();
    if(now>out.deadline) {out.error=ETIMEDOUT;return 1;}
    if(now-out.lastYield>=4000) {out.lastYield=now;vTaskDelay(1);}
    return 0; // Once writing, drain even if the screen goes off or sleep starts.
}
int writeBytes(void *context,const uint8_t *data,size_t length) {
    auto &out=*static_cast<Output *>(context);
    errno=0;
    if(std::fwrite(data,1,length,out.file)==length)return 1;
    out.error=errno?errno:EIO;out.ioError=true;return 0;
}
Notice failure(int error) {
    if(error==ENOMEM)return Notice::Memory;
    if(error==ENOSPC)return Notice::Space;
    if(error==ENODEV || error==EBUSY || error==ENXIO)return Notice::Card;
    return Notice::Failed;
}
void save(Job &j) {
    core::sd::Session card(j.access);
    if(!card) {j.result=failure(card.error());return;}
    if(!core::sd::ensure(core::sd::Screenshots)) {
        const int error=errno?errno:EIO;card.reportError(error);j.result=failure(error);return;
    }
    tm date{};char stamp[32],path[128],temporary[136];
    if(!localtime_r(&j.time.tv_sec,&date) || !std::strftime(stamp,sizeof(stamp),"%Y-%m-%d_%H-%M-%S",&date))return;
    FILE *file=nullptr;
    for(unsigned sequence=0;sequence<100;++sequence) {
        std::snprintf(path,sizeof(path),"%s/%s-%03ld_%02u.png",core::sd::Screenshots,
                      stamp,static_cast<long>(j.time.tv_usec/1000),sequence);
        struct stat st{};
        if(stat(path,&st)==0)continue;
        if(errno!=ENOENT) {const int error=errno;card.reportError(error);j.result=failure(error);return;}
        std::snprintf(temporary,sizeof(temporary),"%s.part",path);
        const int fd=::open(temporary,O_WRONLY|O_CREAT|O_EXCL,0664);
        if(fd<0) {
            if(errno==EEXIST)continue;
            const int error=errno;card.reportError(error);j.result=failure(error);return;
        }
        file=fdopen(fd,"wb");
        if(!file) {
            const int error=errno;::close(fd);std::remove(temporary);
            card.reportError(error);j.result=failure(error);return;
        }
        break;
    }
    if(!file)return;
    const auto now=esp_timer_get_time();
    Output out{file,0,now+10000000,now};
    bool ok=screenshot_png_write(j.pixels,YMCAST_BYTES,writeBytes,cancelled,&out)!=0;
    if(ok && (std::fflush(file)!=0 || fsync(fileno(file))!=0)) {out.error=errno?errno:EIO;out.ioError=true;ok=false;}
    if(std::fclose(file)!=0) {out.error=errno?errno:EIO;out.ioError=true;ok=false;}
    if(ok && std::rename(temporary,path)!=0) {out.error=errno?errno:EIO;out.ioError=true;ok=false;}
    if(!ok) {
        if(!out.error)out.error=ENOMEM; // Fixed valid PNG input: codec allocation failed.
        if(std::remove(temporary)!=0 && errno!=ENOENT)card.reportError(errno);
        if(out.ioError)card.reportError(out.error);
        j.result=failure(out.error);return;
    }
    j.result=Notice::Saved;
} // All handles closed before releasing the card, including every error path.
void worker(void *argument) {
    auto j=static_cast<Job *>(argument);
    {Power::WorkGuard work;save(*j);}
    j->done.store(true,std::memory_order_release); // No further Job access.
    vTaskDeleteWithCaps(nullptr);
}
void capture(lv_event_t *event) {
    if(!job || job->captured || job->invalid || job->writing || !eligible())return;
    const auto area=static_cast<const lv_area_t *>(lv_event_get_param(event));
    auto buffer=lv_display_get_buf_active(display);
    if(!area || !buffer || !ymcast_copy_rect(job->pixels,area->x1,area->y1,area->x2,area->y2,
                                           buffer->data,buffer->header.stride,buffer->data_size)) {
        job->invalid=true;return;
    }
    // A full-screen invalidation produces full-width strips on this display.
    // Refuse incomplete frames rather than saving uninitialized pixel memory.
    if(area->x1==0 && area->x2==YMCAST_WIDTH-1) {
        for(int y=area->y1;y<=area->y2;++y)if(!job->rows[y]) {job->rows[y]=true;++job->rowCount;}
    }
    if(lv_display_flush_is_last(display)) {
        job->captured=job->rowCount==YMCAST_HEIGHT;
        job->invalid=!job->captured;
    }
}
Notice finish(Notice notice) {
    delete job;job=nullptr;active.store(false,std::memory_order_release);return notice;
}
}
void Screenshot::init(lv_display_t *disp) {
    if(display)return;
    display=disp;
    if(display)lv_display_add_event_cb(display,capture,LV_EVENT_FLUSH_START,nullptr);
}
void Screenshot::request() {
    if(eligible() && !active.load(std::memory_order_acquire))requested.store(true,std::memory_order_release);
}
void Screenshot::setViewBlocked(bool blocked) {
    viewBlocked.store(blocked,std::memory_order_release);
    if(blocked)requested.store(false,std::memory_order_release);
}
bool Screenshot::idle() {return !active.load(std::memory_order_acquire) && !requested.load(std::memory_order_acquire);}
Screenshot::Notice Screenshot::poll() {
    const bool request=requested.exchange(false,std::memory_order_acq_rel);
    if(job) {
        if(job->writing) {
            if(job->done.load(std::memory_order_acquire))return finish(job->result);
            return Notice::None;
        }
        if(!eligible())return finish(Notice::None);
        if(job->invalid || esp_timer_get_time()-job->started>1500000)return finish(Notice::Failed);
        if(job->captured) {
            job->writing=true;
            if(xTaskCreateWithCaps(worker,"screenshot",6144,job,2,nullptr,Caps)!=pdPASS)return finish(Notice::Memory);
        }
        return Notice::None;
    }
    if(!request || !eligible())return Notice::None;
    if(core::sd::state()!=core::sd::State::Available)return Notice::Card;
    // Hardware panel orientation leaves LVGL at zero. Capture logical pixels
    // before panel addressing so saved screenshots stay upright in every pose.
    if(!display || lv_display_get_color_format(display)!=LV_COLOR_FORMAT_RGB565 ||
       lv_display_get_render_mode(display)!=LV_DISPLAY_RENDER_MODE_PARTIAL ||
       lv_display_get_rotation(display)!=LV_DISPLAY_ROTATION_0 ||
       lv_display_get_horizontal_resolution(display)!=YMCAST_WIDTH ||
       lv_display_get_vertical_resolution(display)!=YMCAST_HEIGHT)return Notice::Failed;
    job=new(std::nothrow) Job;
    if(!job)return Notice::Memory;
    active.store(true,std::memory_order_release);
    job->access=core::sd::requestAccess(); // Never remount after safe eject.
    gettimeofday(&job->time,nullptr);job->started=esp_timer_get_time();
    job->pixels=static_cast<uint8_t *>(heap_caps_malloc(YMCAST_BYTES,Caps));
    if(!job->pixels)return finish(Notice::Memory);
    lv_display_trigger_activity(display);
    lv_obj_invalidate(lv_display_get_screen_active(display));
    return Notice::Started;
}
}
