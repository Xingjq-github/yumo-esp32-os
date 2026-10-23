#include "fc_saves.hpp"
#include "fc_emulator.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <memory>
extern "C" {
#include "nes/checkpoint.h"
// Keep the legacy core's allocator/IRAM macros out of the C++ frontend.
nes_t *nes_getptr(void);
}
namespace core::fc {
namespace { std::atomic<unsigned> writers{0}; }
bool autoSavesIdle() { return writers.load(std::memory_order_acquire)==0; }
bool requestSave(Job &job,SaveAction action,unsigned slot,const fc_save_slot_t *expected,uint64_t generation) {
  if(!job.ready.load(std::memory_order_acquire) || !job.paused.load() || job.stop.load() ||
     job.done.load() || job.saves.stage.load(std::memory_order_acquire)!=SaveStage::Idle ||
     sys::Power::sleepPending() || slot>=FC_SAVE_SLOTS ||
     (action==SaveAction::Save && slot>=FC_SAVE_MANUAL_SLOTS)) return false;
  auto &s=job.saves;
  s.action=action;s.slot=slot;s.expected=expected?*expected:fc_save_slot_t{};
  s.generation=generation;s.access=sd::requestAccess(true);
  s.success=false;s.message[0]=0;s.cancelled.store(false);
  s.deadline=esp_timer_get_time()+10000000;
  s.stage.store(SaveStage::Pending,std::memory_order_release);
  job.wake.notify();
  return true;
}
namespace {
const char *reason(int error) {
  switch(error) {
  case ESTALE:return "存档或SD卡已变化，请重新打开列表";
  case ENOMEM:return "内存不足，请重试";
  case ENOSPC:return "SD卡空间不足，原存档保留";
  case ECANCELED:return "操作已取消";
  case EBADMSG:return "存档无效或不兼容，当前进度保留";
  default:return "SD卡读写失败，请检查后重试";
  }
}
bool proceed(void *context) {
  const auto &job=*static_cast<Job *>(context);
  vTaskDelay(1);
  return !job.stop.load() && !job.saves.cancelled.load() &&
      esp_timer_get_time()<job.saves.deadline && !sys::Power::sleepPending();
}
bool validate(void *,const void *data,size_t size) { return checkpoint_valid(nes_getptr(),data,size); }
bool capture(void *,void *data,size_t size) { return checkpoint_save(nes_getptr(),data,size); }
bool autoProceed(void *context) {
  const auto &job=*static_cast<Job *>(context);
  vTaskDelay(1);
  // A normal app/menu exit must finish the detached snapshot, not truncate it.
  // Power/SD removal still cancels safely, retaining the previous valid bank.
  return esp_timer_get_time()<job.saves.autoDeadline &&
      !sys::Power::sleepPending() && sd::state()==sd::State::Available;
}
void autoWriter(void *context) {
  {
  std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(context));
  auto &job=**owner;
  auto &s=job.saves;
  bool success=false;int error=0;
  {
    sys::Power::WorkGuard work;
    sd::Session card(s.autoAccess,false);
    error=card?0:(card.error()?card.error():ENODEV);
    if(!error && !card.claimMutation())error=EBUSY;
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> scratch(nullptr,&heap_caps_free);
    if(!error) {
      scratch.reset(static_cast<uint8_t *>(heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
      if(!scratch)error=ENOMEM;
    }
    if(!error && !autoProceed(&job))error=ECANCELED;
    if(!error && !checkpoint_seal(s.autoSnapshot,s.autoProfile.length))error=EBADMSG;
    if(!error) {
      fc_save_store_t store{};store.directory=sd::Games;store.buffer=scratch.get();store.length=s.autoProfile.length;
      std::copy(s.rom.begin(),s.rom.end(),store.rom);store.context=&job;store.proceed=autoProceed;
      fc_save_slot_t saved{};
      success=fc_save_write_snapshot(&store,s.autoSnapshot,s.autoTimestamp,s.autoFrames,job.refreshRate,&saved);
      if(!success)error=store.error?store.error:EIO;
    }
    if(error)card.reportError(error);
  } // All SD/IO scratch references end before publishing completion.
  if(success)s.autoFailed.store(false);
  else if(error!=ECANCELED || esp_timer_get_time()>=s.autoDeadline)s.autoFailed.store(true);
  s.autoBusy.store(false,std::memory_order_release); // last access to snapshot
  } // Release Job and its snapshot BEFORE deleting the FreeRTOS task.
  writers.fetch_sub(1,std::memory_order_release);
  vTaskDeleteWithCaps(nullptr);
}
}
bool processSave(Job &job,uint64_t &playedFrames) {
  auto &s=job.saves;
  if(s.stage.load(std::memory_order_acquire)!=SaveStage::Pending) return false;
  // A slow/removed card must not trap the NES worker in an unbounded drain.
  if(!drainAutoSave(job) || !proceed(&job)) {
    s.success=false;std::snprintf(s.message,sizeof(s.message),"存档忙或操作已取消，请稍后重试");
    s.stage.store(SaveStage::Complete,std::memory_order_release);return false;
  }
  bool loaded=false;
  {
    sys::Power::WorkGuard work;
    sd::Session card(s.access);
    int error=card?0:(card.error()?card.error():ENODEV);
    if(!error && s.action==SaveAction::Save && !card.claimMutation())error=EBUSY;
    if(card && s.action!=SaveAction::Browse && card.generation()!=s.generation) error=ESTALE;
    s.generation=card.generation();
    const size_t size=checkpoint_size(nes_getptr());
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> buffer(nullptr,&heap_caps_free);
    if(!error) {
      if(!size || size>FC_SAVE_MAX) error=EBADMSG;
      else {
        buffer.reset(static_cast<uint8_t *>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
        if(!buffer) error=ENOMEM;
      }
    }
    if(!error) {
      fc_save_store_t store{};
      store.directory=sd::Games;store.buffer=buffer.get();store.length=size;
      std::copy(s.rom.begin(),s.rom.end(),store.rom);
      store.context=&job;store.proceed=proceed;store.validate=validate;store.capture=capture;
      fc_save_slot_t result{};
      if(s.action==SaveAction::Browse) s.success=fc_save_scan(&store,s.slots.data());
      else if(s.action==SaveAction::Save) {
        const time_t now=std::time(nullptr);
        s.success=fc_save_write(&store,s.slot,&s.expected,now>0?uint64_t(now):0,
                                playedFrames,job.refreshRate,&result);
        if(s.success) s.slots[s.slot]=result;
      } else {
        s.success=fc_save_read(&store,s.slot,&s.expected,&result);
        if(s.success) {
          if(!proceed(&job)) { s.success=false;store.error=ECANCELED; }
          else {
            s.success=checkpoint_load(nes_getptr(),buffer.get(),size);
            if(s.success) { playedFrames=result.frames;loaded=true; }
            else store.error=EBADMSG;
          }
        }
      }
      if(!s.success) error=store.error?store.error:EIO;
    }
    if(error) { s.success=false;card.reportError(error); }
    const char *message=error?reason(error):
        s.action==SaveAction::Save?"存档成功":s.action==SaveAction::Load?"读档成功，点击继续游戏":"选择存档位置";
    std::snprintf(s.message,sizeof(s.message),"%s",message);
  } // No file handles, SD reference or checkpoint memory survives the request.
  s.stage.store(SaveStage::Complete,std::memory_order_release);
  return loaded;
}
void prepareAutoSave(Job &job) {
  auto &s=job.saves;
  if(!checkpoint_prepare(nes_getptr(),&s.autoProfile) || !s.autoProfile.length || s.autoProfile.length>FC_SAVE_MAX) {
    s.autoProfile={};s.autoFailed.store(true);return;
  }
  s.autoSnapshot=static_cast<uint8_t *>(heap_caps_malloc(s.autoProfile.length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  s.autoFailed.store(!s.autoSnapshot);
}
bool drainAutoSave(Job &job) {
  const int64_t deadline=esp_timer_get_time()+2000000;
  while(job.saves.autoBusy.load(std::memory_order_acquire)) {
    if(job.stop.load() || job.saves.cancelled.load() || sys::Power::sleepPending() ||
       esp_timer_get_time()>=deadline)return false;
    vTaskDelay(1);
  }
  return true;
}
void releaseAutoSave(Job &job) {
  // Busy writer retains the Job; its final shared-owner destructor frees PSRAM.
  if(job.saves.autoBusy.load(std::memory_order_acquire))return;
  heap_caps_free(job.saves.autoSnapshot);job.saves.autoSnapshot=nullptr;
}
void processAutoSave(Job &job,uint64_t playedFrames) {
  if(!job.ready.load(std::memory_order_acquire) || job.paused.load() || job.stop.load() ||
     job.saves.stage.load(std::memory_order_acquire)!=SaveStage::Idle ||
     sys::Power::sleepPending() || !sys::Power::isScreenOn() ||
     job.saves.autoBusy.load(std::memory_order_acquire))return;
  auto &s=job.saves;
  if(!s.autoProfile.length)return;
  if(!s.autoSnapshot)s.autoSnapshot=static_cast<uint8_t *>(heap_caps_malloc(s.autoProfile.length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!s.autoSnapshot || !checkpoint_capture(nes_getptr(),&s.autoProfile,s.autoSnapshot,s.autoProfile.length)) {
    s.autoFailed.store(true);return;
  }
  const time_t now=std::time(nullptr);
  s.autoFrames=playedFrames;s.autoTimestamp=now>0?uint64_t(now):0;
  s.autoAccess=sd::requestAccess(false);
  auto owner=job.weak_from_this().lock();
  if(!owner) {s.autoFailed.store(true);return;}
  auto holder=new(std::nothrow) std::shared_ptr<Job>(std::move(owner));
  if(!holder) {s.autoFailed.store(true);return;}
  s.autoDeadline=esp_timer_get_time()+10000000;
  s.autoBusy.store(true,std::memory_order_release);
  writers.fetch_add(1,std::memory_order_release);
  if(xTaskCreateWithCaps(autoWriter,"fc-save",6144,holder,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
    delete holder;writers.fetch_sub(1,std::memory_order_release);
    s.autoBusy.store(false,std::memory_order_release);s.autoFailed.store(true);
  }
}
} // namespace core::fc
