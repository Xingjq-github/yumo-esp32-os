// Production save-service decisions; SD/RTOS/checkpoint IO are controlled doubles.
#include "core/fc_emulator.hpp"
#include "core/fc_autosave.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <functional>
#include <cstring>
static unsigned checks,allocations,handles,guards,attempts,notifications;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static bool screen=true,sleeping=false,available=true,mutation=true,allocation=true,io=true,loadable=true;
static int64_t now;
static bool dispatch=true,cancelOnRead=false;
static int64_t esp_timer_get_time(){return now;}
static bool recovery=true,resume=true,taskOk=true,captureOk=true;
static std::function<void()> queued;
static unsigned captures,seals;
static void runWriter(){auto task=std::move(queued);queued={};CHECK(bool(task));task();}
static int errorCode=EIO;
static size_t stateSize=8192;
static uint64_t writtenFrames;
static unsigned writtenSlot;
static std::function<void()> onYield;
namespace sys {struct Power {
 static bool sleepPending(){return sleeping;}static bool isScreenOn(){return screen;}
 struct WorkGuard{WorkGuard(){++guards;}~WorkGuard(){--guards;}};
};}
namespace core {
void WorkerWake::notify(){++notifications;}

namespace sd {
Access requestAccess(bool v){resume=v;return {1,v};}
State state(){return available?State::Available:State::Ejected;}
Session::Session(Access,bool recover,bool){recovery=recover;held_=available;generation_=7;error_=held_?0:ENODEV;if(held_)++handles;}
Session::~Session(){release();}
void Session::release(){if(held_){--handles;held_=false;}}
bool Session::claimMutation(){return mutation;}
void Session::reportError(int){}
}}
static void vTaskDelay(int){now+=1000;if(onYield){auto fn=std::move(onYield);onYield={};fn();}if(dispatch&&queued)runWriter();}
static constexpr int pdPASS=1;
static int xTaskCreateWithCaps(void (*fn)(void *),const char *,int size,void *context,int priority,void *,int){CHECK(size==6144&&priority==1);if(!taskOk)return 0;CHECK(!queued);queued=[=]{fn(context);};return pdPASS;}
static void vTaskDeleteWithCaps(void *){}
static constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static void *heap_caps_malloc(size_t size,int){if(!allocation)return nullptr;++allocations;return std::malloc(size);}
static void heap_caps_free(void *p){if(p){--allocations;std::free(p);}}
namespace core::fc {Job::~Job(){heap_caps_free(saves.autoSnapshot);heap_caps_free(frame);}}
extern "C" {
struct nes_s{};typedef struct nes_s nes_t;
nes_t *nes_getptr(){static nes_t n;return &n;}
size_t checkpoint_size(nes_t *){return stateSize;}
bool checkpoint_prepare(nes_t *,checkpoint_profile_t *p){p->length=stateSize;return stateSize>0;}
bool checkpoint_capture(nes_t *,const checkpoint_profile_t *,void *p,size_t n){++captures;std::memset(p,0xA5,n);return captureOk;}
bool checkpoint_seal(void *p,size_t n){++seals;for(size_t i=0;i<n;++i)CHECK(static_cast<unsigned char *>(p)[i]==0xA5);return true;}
bool checkpoint_valid(nes_t *,const void *,size_t){return true;}
bool checkpoint_save(nes_t *,void *,size_t){return true;}
bool checkpoint_load(nes_t *,const void *,size_t){return loadable;}
bool fc_save_scan(fc_save_store_t *,fc_save_slot_t *out){for(unsigned i=0;i<FC_SAVE_SLOTS;++i){out[i]={};out[i].valid=true;}return true;}
bool fc_save_inspect_slot(fc_save_store_t *s,unsigned slot,fc_save_slot_t *out){CHECK(slot==FC_SAVE_AUTO_SLOT);*out={};if(!s->proceed(s->context)){s->error=ECANCELED;return false;}return true;}
bool fc_save_write(fc_save_store_t *s,unsigned slot,const fc_save_slot_t *,uint64_t,uint64_t frames,uint32_t,fc_save_slot_t *out){
 ++attempts;writtenSlot=slot;writtenFrames=frames;CHECK(handles==1 && guards>0 && allocations==2);
 if(!s->proceed(s->context)){s->error=ECANCELED;return false;}
 if(!io){s->error=errorCode;return false;}*out={};out->valid=true;return true;
}
bool fc_save_write_snapshot(fc_save_store_t *s,const uint8_t *data,uint64_t,uint64_t frames,uint32_t,fc_save_slot_t *out){
 CHECK(data!=s->buffer);++attempts;writtenSlot=FC_SAVE_AUTO_SLOT;writtenFrames=frames;
 CHECK(handles==1&&guards==1&&allocations==2);
 for(unsigned i=0;i<4;++i)if(!s->proceed(s->context)){s->error=ECANCELED;return false;}
 if(!io){s->error=errorCode;return false;}*out={};out->valid=true;return true;
}
bool fc_save_read(fc_save_store_t *s,unsigned,const fc_save_slot_t *,fc_save_slot_t *out){*out={};out->valid=true;out->frames=4321;if(cancelOnRead)static_cast<core::fc::Job *>(s->context)->saves.cancelled=true;return true;}
}
#include "service.inc"
using namespace core::fc;
static void resident(){CHECK(allocations==1&&!handles&&!guards&&!queued);}
static void clean(){CHECK(!allocations&&!handles&&!guards&&!queued);}
static void baseline(){screen=true;sleeping=false;available=true;mutation=true;allocation=true;io=true;loadable=true;taskOk=true;captureOk=true;stateSize=8192;onYield={};dispatch=true;cancelOnRead=false;}
int main(){
 for(unsigned cycle=0;cycle<100;++cycle){
  baseline();auto owner=std::make_shared<Job>();Job &j=*owner;j.ready=true;j.paused=false;prepareAutoSave(j);resident();
  const auto before=attempts,cs=captures,ss=seals;
  processAutoSave(j,3600);CHECK(attempts==before&&captures==cs+1&&seals==ss&&j.saves.autoBusy&&queued);
  CHECK(!handles&&!guards&&allocations==1); // NES already continues before CRC/SD
  processAutoSave(j,3601);CHECK(captures==cs+1); // one in-flight snapshot only
  runWriter();CHECK(attempts==before+1&&writtenSlot==FC_SAVE_AUTO_SLOT&&writtenFrames==3600);
  CHECK(!recovery&&!resume&&!j.saves.autoFailed&&!j.saves.autoBusy);resident();
  j.paused=true;CHECK(!requestSave(j,SaveAction::Save,FC_SAVE_AUTO_SLOT));
  CHECK(requestSave(j,SaveAction::Save,2,nullptr,7));uint64_t frames=5000;
  CHECK(!processSave(j,frames));CHECK(j.saves.success&&writtenSlot==2);resident();
  j.saves.stage=SaveStage::Idle;CHECK(requestSave(j,SaveAction::Load,FC_SAVE_AUTO_SLOT,nullptr,7));
  CHECK(processSave(j,frames));CHECK(frames==4321&&j.saves.success);resident();
  j.saves.stage=SaveStage::Idle;j.paused=false;
  for(unsigned fault=0;fault<6;++fault){
   baseline();if(fault==0)available=false;if(fault==1)mutation=false;if(fault==2)allocation=false;
   if(fault==3)io=false;if(fault==4)taskOk=false;if(fault==5)captureOk=false;
   processAutoSave(j,6000);if(queued)runWriter();CHECK(j.saves.autoFailed);CHECK(j.saves.stage==SaveStage::Idle);resident();
  }
  baseline();processAutoSave(j,7000);runWriter();CHECK(!j.saves.autoFailed);resident();
  // Pause does not interrupt a snapshot already detached from the live machine.
  processAutoSave(j,8000);j.paused=true;runWriter();CHECK(!j.saves.autoFailed);resident();
  j.paused=false;processAutoSave(j,8100);j.paused=true;CHECK(requestSave(j,SaveAction::Load,FC_SAVE_AUTO_SLOT,nullptr,7));
  const auto writes=attempts;CHECK(processSave(j,frames));CHECK(attempts==writes+1&&j.saves.success&&!j.saves.autoBusy);resident();
  j.saves.stage=SaveStage::Idle;j.paused=false;
  for(unsigned block=0;block<5;++block){
   baseline();j.ready=true;j.stop=false;j.paused=false;
   if(block==0)j.ready=false;if(block==1)j.stop=true;if(block==2)j.paused=true;if(block==3)screen=false;if(block==4)sleeping=true;
   const auto count=captures;processAutoSave(j,9000);CHECK(captures==count);resident();
  }
  baseline();j.ready=true;j.stop=false;j.paused=false;
  // A stalled writer cannot keep a save dialog request pending forever.
  processAutoSave(j,9500);j.paused=true;dispatch=false;
  CHECK(requestSave(j,SaveAction::Browse));const auto waitStart=now;
  CHECK(!processSave(j,frames));CHECK(!j.saves.success && now-waitStart>=2000000 && now-waitStart<2100000);
  CHECK(j.saves.stage==SaveStage::Complete && j.saves.autoBusy && !autoSavesIdle());
  dispatch=true;runWriter();j.saves.stage=SaveStage::Idle;j.paused=false;
  processAutoSave(j,10000);const auto exitingWrites=attempts;j.stop=true;releaseAutoSave(j);
  CHECK(attempts==exitingWrites && j.saves.autoBusy && j.saves.autoSnapshot && !autoSavesIdle());
  std::weak_ptr<Job> weak=owner;owner.reset();CHECK(!weak.expired());
  runWriter();CHECK(attempts==exitingWrites+1&&writtenFrames==10000);
  CHECK(weak.expired() && autoSavesIdle());clean();
 }
 // Deadline/cancel/repeated pause are independent of filesystem speed.
 {
  baseline();auto owner=std::make_shared<Job>();auto &j=*owner;
  const auto n=notifications;j.setPaused(true);j.setPaused(true);CHECK(notifications==n+1);
  j.ready=true;prepareAutoSave(j);CHECK(requestSave(j,SaveAction::Browse));j.saves.cancelled=true;
  uint64_t frames=0;CHECK(!processSave(j,frames) && !j.saves.success);
  j.saves.stage=SaveStage::Idle;CHECK(requestSave(j,SaveAction::Browse));now+=10000000;
  CHECK(!processSave(j,frames) && !j.saves.success);j.saves.stage=SaveStage::Idle;
  j.paused=false;processAutoSave(j,3600);now+=10000000;const auto nwrite=attempts;runWriter();
  CHECK(attempts==nwrite && !j.saves.autoBusy && j.saves.autoFailed && autoSavesIdle());
  j.paused=true;CHECK(requestSave(j,SaveAction::Load,0,nullptr,7));cancelOnRead=true;
  CHECK(!processSave(j,frames) && !j.saves.success && frames==0);releaseAutoSave(j);clean();
 }
 for(size_t size:{size_t(0),size_t(FC_SAVE_MAX+1)}){
  baseline();stateSize=size;Job j;prepareAutoSave(j);CHECK(j.saves.autoFailed);releaseAutoSave(j);clean();
 }
 std::printf("%u checks: detached snapshot before CRC/IO, no task overlap, bounded drain, detached exit lifetime, faults, 100 exit/reentry resource cycles.\n",checks);
}
