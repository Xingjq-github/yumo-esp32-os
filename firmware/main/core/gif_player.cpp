#include "gif_player.hpp"
#include "photo_gif.h"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <cerrno>
#include <cstdio>
#include <new>
#include <strings.h>
#include <sys/stat.h>
namespace core::gif {
namespace {
std::atomic<bool> active{false};
bool stopping(Job &j) {return j.cancelled.load() || sys::Power::sleepPending() || sd::busy();}
struct Stream {
    Job &job;
    FILE *file=nullptr;
    photo_gif *decoder=nullptr;
    int64_t frameAt=0,yieldAt=0;
    bool timedOut=false;
    ~Stream() {photo_gif_close(decoder);if(file)fclose(file);}
};
int cooperate(void *arg) {
    auto &s=*static_cast<Stream *>(arg);
    const auto now=esp_timer_get_time();
    if(now-s.yieldAt>=3000) {vTaskDelay(1);s.yieldAt=now;}
    if(now-s.frameAt>4000000)s.timedOut=true;
    return stopping(s.job)||s.timedOut;
}
void execute(Job &job) {
    sd::Session card(job.access);
    if(!card) {job.error="SD卡不可用，请返回后刷新";return;}
    Stream stream{job};
    struct stat st{};
    if(stat(job.path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>64*1024*1024) {
        job.error="GIF不存在或超过64MB";return;
    }
    stream.file=fopen(job.path.c_str(),"rb");
    if(!stream.file) {card.reportError(errno);job.error="GIF读取失败";return;}
    stream.frameAt=stream.yieldAt=esp_timer_get_time();
    stream.decoder=photo_gif_open(&stream,[](void *p,uint8_t *out,int n) {
        return int(fread(out,1,n,static_cast<Stream *>(p)->file));
    },[](void *p) {return fseek(static_cast<Stream *>(p)->file,0,SEEK_SET);},cooperate);
    if(!stream.decoder) {job.error="GIF无效、尺寸超过1024或内存不足";return;}
    job.width=photo_gif_width(stream.decoder);job.height=photo_gif_height(stream.decoder);
    const size_t count=job.width*job.height;
    for(auto &p:job.pixels)p=static_cast<uint16_t *>(heap_caps_malloc(count*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!job.pixels[0]||!job.pixels[1]) {job.error="GIF播放内存不足";return;}
    unsigned frames=0;
    while(!stopping(job)) {
        // Decode into the spare buffer as soon as the GUI accepts a frame.
        // The GUI keeps the next frame queued until its presentation deadline.
        while(job.ready.load(std::memory_order_acquire)>=0 && !stopping(job))vTaskDelay(pdMS_TO_TICKS(2));
        if(stopping(job))break;
        const int index=job.displayed.load(std::memory_order_acquire)==0?1:0;
        unsigned delay=100;
        stream.frameAt=esp_timer_get_time();
        const int rc=photo_gif_next(stream.decoder,job.pixels[index],count,&delay);
        if(rc==0 && frames) {
            frames=0;
            if(photo_gif_restart(stream.decoder))continue;
        }
        if(rc!=1) {
            if(!stopping(job))job.error=stream.timedOut?"GIF解码超时":"GIF文件损坏或读取失败";
            if(ferror(stream.file))card.reportError(EIO);
            break;
        }
        ++frames;
        job.delayMs=delay;
        job.ready.store(index,std::memory_order_release);
    }
}
void worker(void *arg) {
    {
        std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(arg));
        auto job=std::move(*owner);owner.reset();
        {sys::Power::WorkGuard work;
         try {execute(*job);} catch(const std::bad_alloc &) {job->error="GIF播放内存不足";}}
        job->done.store(true,std::memory_order_release);
    }
    active.store(false,std::memory_order_release);
    vTaskDeleteWithCaps(nullptr);
}
}
bool isFile(const std::string &path) {return path.size()>4 && strcasecmp(path.c_str()+path.size()-4,".gif")==0;}
Job::~Job() {for(auto p:pixels)heap_caps_free(p);}
bool idle() {return !active.load(std::memory_order_acquire);}
std::shared_ptr<Job> start(const std::string &path) {
    if(sys::Power::sleepPending() || !isFile(path) || path.rfind("/sdcard/",0)!=0 ||
       path.find("/../")!=std::string::npos || path.find('\0')!=std::string::npos || active.exchange(true))return {};
    std::shared_ptr<Job> job;
    try {
        job=std::make_shared<Job>();job->path=path;job->access=sd::requestAccess();
        auto owner=new std::shared_ptr<Job>(job);
        if(xTaskCreateWithCaps(worker,"gif_player",6144,owner,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete owner;
    } catch(const std::bad_alloc &) {}
    active.store(false,std::memory_order_release);return {};
}
}
