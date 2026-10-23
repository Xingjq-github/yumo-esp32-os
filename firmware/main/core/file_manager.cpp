#include "file_manager.hpp"
#include "storage_browser.hpp"
#include "audio_recorder.hpp"
#include "fc_emulator.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <cerrno>
#include <new>
namespace core::files {
namespace {
std::atomic<bool> active{false};
void worker(void *arg) {
    {
        std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(arg));
        auto job=std::move(*owner);owner.reset();
        {
            sys::Power::WorkGuard work;
            sd::Session card(job->access);
            if(!card)job->result.error=card.error();
            else if(job->request.operation!=Operation::List &&
                    (!job->generation || job->generation!=card.generation()))job->result.error=ENODEV;
            else if(mutates(job->request.operation) && !card.claimMutation())job->result.error=EBUSY;
            else {
                job->generation=card.generation();
                struct Context {Job *job;int64_t yielded=0;} context{job.get()};
                Hooks hooks{&context,[](void *p) {
                    auto &ctx=*static_cast<Context *>(p);const auto now=esp_timer_get_time();
                    if(now-ctx.yielded>=4000){vTaskDelay(1);ctx.yielded=now;}
                    return ctx.job->cancelled.load() || sys::Power::sleepPending() || sd::busy();
                },[](void *p,uint64_t bytes,unsigned changed) {
                    auto j=static_cast<Context *>(p)->job;j->kilobytes.store(uint32_t(bytes/1024));j->changed.store(changed);
                }};
                job->result=execute(sd::Root,job->request,hooks);
                card.reportError(job->result.error);
            }
        } // All FILE/DIR handles and the mutation fence close before done.
        job->done.store(true,std::memory_order_release);
    }
    active.store(false,std::memory_order_release);vTaskDeleteWithCaps(nullptr);
}
}
bool idle(){return !active.load(std::memory_order_acquire);}
std::shared_ptr<Job> start(Request request,uint64_t generation) {
    // A previous App can still be draining its last bookmark/recording/save.
    // Check queued workers as well as open handles, BEFORE claiming our slot;
    // storageIdle includes this worker, so checking it afterwards would deadlock.
    if(request.operation!=Operation::List &&
       (!core::storageIdle() || !core::recorder::idle() || !core::fc::idle()))return {};
    if(sys::Power::sleepPending() || active.exchange(true))return {};
    try {
        auto job=std::make_shared<Job>();job->request=std::move(request);job->generation=generation;
        job->access=sd::requestAccess(job->request.operation==Operation::List);
        auto owner=new std::shared_ptr<Job>(job);
        if(xTaskCreateWithCaps(worker,"file_manager",10240,owner,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete owner;
    }catch(const std::bad_alloc &){}
    active.store(false,std::memory_order_release);return {};
}
const char *errorText(int error) {
    switch(error) {
    case 0:return "操作完成";
    case ECANCELED:return "已取消；已完成的删除无法恢复";
    case EEXIST:return "同名项目已存在，请更换名称";
    case ENOSPC:return "SD卡空间不足";
    case ENOMEM:return "内存不足，请稍后重试";
    case EINVAL:return "名称或目标无效，不能操作自身或子目录";
    case ENODEV:case ENOENT:case ENOTDIR:return "存储卡或项目已变化，请刷新";
    case EBUSY:return "SD卡正在使用，请稍后刷新重试";
    case ELOOP:case E2BIG:return "目录过深或项目过多，请分批操作";
    default:return "文件操作失败，请检查SD卡后刷新";
    }
}
}
