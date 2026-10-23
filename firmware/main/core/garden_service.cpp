#include "garden_service.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <cerrno>
#include <new>
namespace core::garden {
namespace {
std::atomic<bool> running{false};
void execute(Job &j){
    if(!j.saving && (j.cancelled.load() || sys::Power::sleepPending())){j.error=ECANCELED;return;}
    sys::Power::WorkGuard work;
    sd::Session card(j.access,true,j.saving);
    if(!card){j.error=card.error();return;}
    if(j.saving){
        if(!j.generation || j.generation!=card.generation()){j.error=ESTALE;return;}
        if(!card.claimMutation()){j.error=EBUSY;return;}
        if(!sd::ensure(sd::Garden)){j.error=errno?errno:EIO;card.reportError(j.error);return;}
        j.error=store::save(sd::Garden,j.state,j.expected,j.result);
    }else{
        j.generation=card.generation();j.error=store::load(sd::Garden,j.result);
    }
    card.reportError(j.error);
}
void worker(void *context){
    {std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(context));auto job=*owner;
        try{execute(*job);}catch(const std::bad_alloc &){job->error=ENOMEM;}
        // Finished jobs own no FILE/SD/work lease. Publish idle before GUI starts a successor.
        running.store(false,std::memory_order_release);job->done.store(true,std::memory_order_release);
    }
    vTaskDeleteWithCaps(nullptr);
}
std::shared_ptr<Job> submit(std::shared_ptr<Job> job){
    bool expected=false;if(!running.compare_exchange_strong(expected,true))return {};
    auto owner=new(std::nothrow) std::shared_ptr<Job>(job);
    if(owner && xTaskCreateWithCaps(worker,"garden_store",6144,owner,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
    delete owner;running.store(false,std::memory_order_release);return {};
}
}
bool idle(){return !running.load(std::memory_order_acquire);}
std::shared_ptr<Job> open(){try{auto j=std::make_shared<Job>();j->access=sd::requestAccess(true);return submit(j);}catch(const std::bad_alloc &){return {};}}
std::shared_ptr<Job> save(const State &state,uint64_t revision,uint64_t generation,sd::Access access){
    if(!generation || !valid(state))return {};
    try{auto j=std::make_shared<Job>();j->saving=true;j->state=state;j->expected=revision;j->generation=generation;j->access=access;return submit(j);}catch(const std::bad_alloc &){return {};}
}
}
