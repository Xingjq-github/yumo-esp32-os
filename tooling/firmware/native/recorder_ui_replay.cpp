// Real Recorder and LVGL event/state transitions. SD, audio jobs and navigation
// are fakes; no real recordings are opened or deleted by this replay.
#include "lvgl.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#ifndef ESTALE
#define ESTALE 116 // Host CRT lacks the POSIX stale-file code used by ESP-IDF.
#endif
#include "core/audio_recorder.hpp"
#include "core/storage_browser.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "ui/recorder_layout.hpp"
#include "ui/control_states.h"
#define private public
#include "apps/recorder.hpp"
#undef private
#include "core/wav_format.hpp"
static unsigned checks=0,home=0,deleted=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
// Audio worker is a test double in this UI replay.
namespace core { void WorkerWake::notify() {} }
namespace fake {
bool screen=true,audioIdle=true;
int listError=0;bool delayedList=false;
std::vector<core::FileEntry> entries={
  {"/sdcard/recordings/test.wav","test.wav",false,{100,200,true}},
  {"/sdcard/recordings/other.wav","other.wav",false,{300,400,true}}};
core::FileEntry expectedDelete;
std::shared_ptr<core::StorageJob> lastList;
}
namespace sys {struct Power {static bool isScreenOn(){return fake::screen;}static bool sleepPending(){return false;}};}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::goHome(){++home;}
void TextInput::bindField(lv_obj_t *){}
}
namespace core {
std::shared_ptr<StorageJob> listMedia(bool games) {
  CHECK(!games);auto job=std::make_shared<StorageJob>();job->cardGeneration=9;
  job->entries=fake::entries;job->error=fake::listError;
  if(job->error)job->entries.clear();
  job->done=!fake::delayedList;fake::lastList=job;return job;
}
const char *storageError(int){return "test error";}
std::shared_ptr<StorageJob> deleteRecording(const std::string &path,uint64_t generation,FileIdentity identity){
  CHECK(path==fake::expectedDelete.path&&generation==9&&fake::audioIdle);
  CHECK(identity.valid&&identity.size==fake::expectedDelete.identity.size&&identity.modified==fake::expectedDelete.identity.modified);
  ++deleted;
  fake::entries.erase(std::remove_if(fake::entries.begin(),fake::entries.end(),[&](const FileEntry &e){return e.path==path;}),fake::entries.end());
  auto job=std::make_shared<StorageJob>();job->done=true;return job;
}
namespace sd {State state(){return State::Available;}}
namespace recorder {
bool idle(){return fake::audioIdle;}
std::shared_ptr<Job> start(bool recording,const std::string &path){
  auto job=std::make_shared<Job>(recording);job->path=path;
  job->state=recording?State::Recording:State::Playing;return job;
}
}
}
#include "compact_ui.inc"
#include "recorder.inc"
static lv_indev_t *touch=nullptr;
static bool pressed=false;static int px=0,py=0;
static uint8_t buffer[240*240*2];
static void settle(){lv_tick_inc(120);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void sample(bool down,lv_obj_t *button){lv_area_t a{};lv_obj_get_coords(button,&a);px=(a.x1+a.x2)/2;py=(a.y1+a.y2)/2;pressed=down;lv_tick_inc(20);lv_indev_read(touch);}
static void tap(lv_obj_t *button){sample(true,button);sample(false,button);}
static size_t available(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static void released(apps::Recorder &app) {
  for(auto button:{app.rewind,app.pause,app.forward})CHECK(!(lv_obj_get_state(button)&(LV_STATE_PRESSED|LV_STATE_CHECKED)));
}
int main(){
  lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
  touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch,[](lv_indev_t *,lv_indev_data_t *d){d->point={px,py};d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
  ui::theme::init(10);settle();
  const auto baseTimers=timers();unsigned rounds=0;
  for(int theme:{4,5,6,8,10}) {
    ui::theme::init(theme);settle();size_t baseline=0;
    for(int cycle=0;cycle<31;++cycle) {
      auto root=ui::compact::panel(lv_screen_active(),240,240);apps::Recorder app;app.onCreate(root);settle();
      app.play(0);CHECK(app.job&&!app.job->recording);app.poll();settle();
      for(int direction:{-1,1}) {
        auto button=direction<0?app.rewind:app.forward;
        CHECK(!lv_obj_has_state(button,LV_STATE_DISABLED));
        sample(true,button);CHECK(app.job->scrub==direction);CHECK(lv_obj_has_state(button,LV_STATE_PRESSED));
        auto old=app.job;old->ok=cycle%2;old->error="test playback failure";old->state=core::recorder::State::Finished;old->done=true;
        app.poll();CHECK(!app.job);released(app);CHECK(old->scrub==0);
        // End arrives while still holding. Release must not leave a highlight
        // or act on a subsequent newly created audio Job.
        sample(false,button);app.poll();settle();released(app);
        app.play(0);app.poll();settle();CHECK(app.job);released(app);
      }
      app.job->pause=true;app.poll();sample(true,app.rewind);CHECK(app.job->scrub==-1);
      sample(false,app.rewind);CHECK(app.job->pause&&app.job->scrub==0);app.poll();released(app);
      sample(true,app.forward);CHECK(app.job->scrub==1);
      app.askDelete(0);CHECK(app.dialog&&app.job->scrub==0);released(app);
      sample(false,app.forward);app.closeDelete();app.poll();CHECK(deleted==0);
      sample(true,app.rewind);auto old=app.job;CHECK(old->scrub==-1);
      app.onHide();CHECK(old->stop&&old->scrub==0);released(app);
      sample(false,app.rewind);app.onDestroy();lv_obj_delete(root);settle();
      CHECK(timers()==baseTimers);if(!baseline)baseline=available();else CHECK(available()>=baseline);
      ++rounds;
    }
  }
  CHECK(deleted==0);
  for(int theme:{4,5,6,8,10}) {
    ui::theme::init(theme);
    fake::entries.clear();
    for(int i=0;i<5;++i) {
      char name[64];std::snprintf(name,sizeof(name),"2026-09-30_12-20-00_100_%02d.wav",i);
      fake::entries.push_back({std::string("/sdcard/recordings/")+name,name,false,{uint64_t(100+i),int64_t(200+i),true}});
    }
    auto root=ui::compact::panel(lv_screen_active(),240,240);apps::Recorder app;app.onCreate(root);settle();
    CHECK(app.files.size()==5);auto oldCount=deleted;
    auto row=lv_obj_get_child(app.list,0);auto fileLabel=lv_obj_get_child(lv_obj_get_child(row,0),0);
    CHECK(std::string(lv_label_get_text(fileLabel))=="2026-09-30\n12-20-00_100_04");
    CHECK(lv_obj_get_height(fileLabel)<=ui::recorder_layout::RowHeight);
    // Repeating a tap at the right-side trash position must not also hit the
    // destructive confirmation when the modal replaces the list underneath.
    fake::expectedDelete=app.files[app.files.size()-2];
    auto trash=lv_obj_get_child(lv_obj_get_child(app.list,1),1);
    tap(trash);CHECK(app.dialog);lv_obj_update_layout(app.dialog);
    tap(trash);app.poll();CHECK(deleted==oldCount);
    if(app.dialog){CHECK(app.onBack());app.poll();}
    settle();row=lv_obj_get_child(app.list,0);
    // Real pointer events choose the newest reversed row; cancel writes nothing.
    tap(lv_obj_get_child(row,1));CHECK(app.dialog);CHECK(app.deletePath==fake::entries.back().path);
    CHECK(lv_label_get_long_mode(lv_obj_get_child(lv_obj_get_child(app.dialog,1),0))==LV_LABEL_LONG_MODE_WRAP);
    lv_obj_update_layout(app.dialog);tap(lv_obj_get_child(app.dialog,-2));CHECK(!app.dialog&&deleted==oldCount);
    // Repeated confirmation deletes one snapshot, then the next newest row.
    for(int i=0;i<2;++i) {
      fake::expectedDelete=fake::entries.back();row=lv_obj_get_child(app.list,0);
      tap(lv_obj_get_child(row,1));CHECK(app.dialog);lv_obj_update_layout(app.dialog);
      tap(lv_obj_get_child(app.dialog,-1));app.poll();settle();
      CHECK(!app.dialog&&deleted==oldCount+unsigned(i+1));CHECK(app.files.size()==size_t(4-i));
    }
    // EOF while the modal is open must keep its file snapshot; wait until the
    // audio worker releases its handles even after it publishes done.
    fake::expectedDelete=fake::entries.back();app.play(app.files.size()-1);app.poll();settle();
    auto playing=app.job;CHECK(playing);fake::audioIdle=false;
    tap(lv_obj_get_child(lv_obj_get_child(app.list,0),1));CHECK(app.dialog);
    playing->done=true;app.poll();CHECK(app.dialog&&!app.job&&!app.listing);
    lv_obj_update_layout(app.dialog);tap(lv_obj_get_child(app.dialog,-1));
    CHECK(deleted==oldCount+2&&!app.pendingDeletePath.empty());
    fake::audioIdle=true;app.poll();app.poll();settle();CHECK(deleted==oldCount+3&&app.files.size()==2);
    // Failed enumeration neither clears the visible directory nor permits a
    // stale playback/delete. Refresh itself must stay available for recovery.
    fake::listError=EIO;app.refresh();app.poll();settle();
    CHECK(app.files.size()==2&&!app.filesGeneration);CHECK(!lv_obj_has_state(app.refreshButton,LV_STATE_DISABLED));
    app.askDelete(0);app.play(0);CHECK(!app.dialog&&!app.job);
    row=lv_obj_get_child(app.list,0);CHECK(lv_obj_has_state(lv_obj_get_child(row,1),LV_STATE_DISABLED));
    fake::listError=0;tap(app.refreshButton);app.poll();settle();CHECK(app.filesGeneration==9);
    // Hold an old row while an asynchronous list refresh finishes. Rebuilding
    // invalidates the pressed object, so release cannot delete a different row.
    row=lv_obj_get_child(app.list,0);sample(true,lv_obj_get_child(row,1));
    fake::delayedList=true;app.refresh();app.poll();
    fake::lastList->done=true;app.poll();sample(false,app.refreshButton);settle();
    CHECK(!app.dialog&&deleted==oldCount+3);fake::delayedList=false;
    // Exit while a confirmed playing-file deletion waits for close cancels it.
    app.play(0);app.poll();settle();fake::audioIdle=false;playing=app.job;
    app.askDelete(0);CHECK(app.dialog);lv_obj_update_layout(app.dialog);tap(lv_obj_get_child(app.dialog,-1));
    CHECK(playing->stop&&!app.pendingDeletePath.empty());app.onHide();playing->done=true;
    CHECK(app.pendingDeletePath.empty());fake::audioIdle=true;
    app.onDestroy();lv_obj_delete(root);settle();CHECK(deleted==oldCount+3&&timers()==baseTimers);
  }
  fake::lastList.reset();lv_indev_delete(touch);lv_display_delete(display);lv_deinit();
  std::printf("%u checks; %u transport lifecycles plus five-theme confirmed deletion/cancel/EOF/refresh/exit replays; %u exact targets deleted by fake storage only.\n",checks,rounds,deleted);
}
