#include "recorder.hpp"
#include "core/power_mgr.hpp"
#include "core/wav_format.hpp"
#include "ui/compact_ui.hpp"
#include "ui/recorder_layout.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
namespace apps {
namespace c=ui::compact;
namespace layout=ui::recorder_layout;
namespace {
constexpr int MeterMax=1000;
void iconSize(lv_obj_t *button,const lv_font_t *font=&lv_font_montserrat_24) {
  auto label=lv_obj_get_child(button,0);
  lv_obj_set_style_text_font(label,font,0);lv_obj_center(label);
}
void place(lv_obj_t *obj,layout::Rect rect) {
  lv_obj_set_pos(obj,rect.x,rect.y);lv_obj_set_size(obj,rect.w,rect.h);
}
}
void Recorder::onCreate(lv_obj_t *parent) {
  root=parent;
  lv_obj_set_scrollable(parent,false);
  auto header=c::panel(parent,240,36);
  back=c::button(header,LV_SYMBOL_LEFT,[](lv_event_t *e) {
    auto self=static_cast<Recorder *>(lv_event_get_user_data(e));
    if(!self->dialog&&!self->deleting)ui::WindowManager::instance().goHome();
  },this,36,36);
  status=c::label(header,"开始录音 / 点击列表播放",153);
  lv_obj_set_pos(status,40,6);
  lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  refreshButton=c::button(header,LV_SYMBOL_REFRESH,[](lv_event_t *e) {
    static_cast<Recorder *>(lv_event_get_user_data(e))->refresh();
  },this,42,36);
  lv_obj_set_pos(refreshButton,198,0);
  iconSize(back);iconSize(refreshButton);
  elapsed=c::label(parent,"00:00:00");
  lv_obj_set_pos(elapsed,4,39);
  lv_obj_set_style_text_font(elapsed,&lv_font_montserrat_18,0);
  meter=lv_bar_create(parent);lv_bar_set_range(meter,0,MeterMax);
  lv_obj_set_style_bg_color(meter,ui::theme::surface(),0);
  lv_obj_set_style_bg_color(meter,ui::theme::accent(),LV_PART_INDICATOR);
  record=c::button(parent,"",click,this,layout::Record.w,layout::Record.h);
  place(record,layout::Record);
  c::buttonBackground(record,ui::theme::tiles()?ui::theme::accent():ui::theme::surface());
  if(ui::theme::focus()) {
    // Static cassette outline around the live record/pause symbol. Keep the
    // original touch targets and full recording list; no spinning reel timer.
    auto tape=c::panel(record,106,53);lv_obj_center(tape);
    lv_obj_set_style_radius(tape,8,0);lv_obj_set_style_bg_opa(tape,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(tape,1,0);lv_obj_set_style_border_color(tape,ui::theme::edge(),0);
    lv_obj_set_clickable(tape,false);
    for(int x:{6,82}) {
      auto reel=c::panel(tape,16,16);lv_obj_set_pos(reel,x,17);
      lv_obj_set_style_radius(reel,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(reel,2,0);
      lv_obj_set_style_border_color(reel,ui::theme::muted(),0);
      lv_obj_set_clickable(reel,false);
    }
  }
  // Draw the solid recording circle: no Unicode/font fallback dependency.
  recordDot=lv_obj_create(record);lv_obj_remove_style_all(recordDot);
  lv_obj_set_size(recordDot,28,28);lv_obj_set_style_radius(recordDot,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_color(recordDot,ui::theme::tiles()?ui::theme::onAccent():ui::theme::danger(),0);
  lv_obj_set_style_bg_opa(recordDot,LV_OPA_COVER,0);
  lv_obj_set_clickable(recordDot,false);
  lv_obj_set_scrollable(recordDot,false);
  lv_obj_center(recordDot);
  recordPause=c::label(record,LV_SYMBOL_PAUSE);
  lv_obj_set_style_text_font(recordPause,&lv_font_montserrat_24,0);
  lv_obj_center(recordPause);lv_obj_set_hidden(recordPause,true);
  stop=c::button(parent,LV_SYMBOL_STOP,click,this,121,40);
  iconSize(stop,&lv_font_montserrat_24);
  place(stop,layout::Stop);
  rewind=c::button(parent,LV_SYMBOL_LEFT LV_SYMBOL_LEFT,nullptr,nullptr,39,36);
  pause=c::button(parent,LV_SYMBOL_PAUSE,click,this,40,36);
  forward=c::button(parent,LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT,nullptr,nullptr,40,36);
  for(auto button:{rewind,pause,forward})iconSize(button);
  place(rewind,layout::Rewind);place(pause,layout::Pause);place(forward,layout::Forward);
  lv_obj_add_event_cb(rewind,seek,LV_EVENT_ALL,this);
  lv_obj_add_event_cb(forward,seek,LV_EVENT_ALL,this);
  list=c::panel(parent,layout::List.w,layout::List.h,true);place(list,layout::List);
  lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
  timer=lv_timer_create(tick,100,this);
  refresh();poll();
}
void Recorder::updateMeter() {
  lv_obj_update_layout(elapsed);
  const int x=lv_obj_get_x(elapsed)+lv_obj_get_width(elapsed)+layout::MeterGap;
  lv_obj_set_pos(meter,x,lv_obj_get_y(elapsed)+(lv_obj_get_height(elapsed)-12)/2);
  lv_obj_set_size(meter,std::max(1,236-x),12);
}
void Recorder::refresh() {
  if(job||dialog||deleting||!pendingDeletePath.empty()||(listing&&!listing->done.load()))return;
  listing=core::listMedia(false);
  if(lv_tick_get()>=noticeUntil)c::setText(status,"正在读取录音列表");
}
void Recorder::play(size_t index) {
  if(job||dialog||deleting||listing||!filesGeneration||!pendingDeletePath.empty()||index>=files.size())return;
  playPath=files[index].path;
  job=core::recorder::start(false,files[index].path);
  c::setText(status,job?"正在打开录音":"音频任务忙，请稍后重试");
  poll();
}
void Recorder::closeDelete() {
  if(dialog)lv_obj_delete(dialog);
  dialog=nullptr;deletePath.clear();deleteIdentity={};
}
void Recorder::askDelete(size_t index) {
  if(dialog||deleting||listing||!filesGeneration||recordAfterPlayback||!pendingDeletePath.empty()||index>=files.size())return;
  if(!files[index].identity.valid)return;
  if(job&&(job->recording||job->stop.load()||files[index].path!=playPath))return;
  if(job)job->setScrub(0);
  clearTransportPress();
  deletePath=files[index].path; // stable target, never a later refreshed row index
  deleteIdentity=files[index].identity;
  dialog=c::panel(root,240,240);
  lv_obj_set_gesture_bubble(dialog,false);
  auto question=c::label(dialog,"是否删除这条录音？",230);
  lv_obj_set_pos(question,5,48);
  auto filename=c::label(dialog,files[index].name.c_str(),230);
  lv_obj_set_pos(filename,5,82);
  lv_obj_set_style_text_font(filename,&lv_font_montserrat_12,0);
  // Show the full name, including milliseconds/suffix, before destructive work.
  // Very long imported names can be scrolled without moving the action buttons.
  lv_label_set_long_mode(filename,LV_LABEL_LONG_MODE_WRAP);
  auto nameArea=c::panel(dialog,230,62,true);lv_obj_set_pos(nameArea,5,76);
  lv_obj_set_parent(filename,nameArea);lv_obj_set_pos(filename,0,0);
  auto warning=c::label(dialog,"删除后无法恢复",230);lv_obj_set_pos(warning,5,140);
  auto cancel=c::button(dialog,"取消",[](lv_event_t *e) {
    auto self=static_cast<Recorder *>(lv_event_get_user_data(e));
    self->closeDelete();self->poll();
  },this,119,44);
  lv_obj_set_pos(cancel,121,164);
  auto confirm=c::button(dialog,"确认删除",[](lv_event_t *e) {
    auto self=static_cast<Recorder *>(lv_event_get_user_data(e));
    if(!self->dialog||self->deletePath.empty()||self->deleting||!self->pendingDeletePath.empty())return;
    // Keep both identity and card generation stable while stopping playback.
    // Never remove an open file or a same-named file on replacement media.
    self->pendingDeletePath=self->deletePath;
    self->pendingDeleteIdentity=self->deleteIdentity;
    self->pendingDeleteGeneration=self->filesGeneration;
    self->recordAfterPlayback=false;
    if(self->job){self->job->setScrub(0);self->job->requestStop();}
    self->closeDelete();
    c::setText(self->status,"正在停止播放并删除录音");
    self->poll();
  },this,120,44);
  // Trash targets are on the right of the list. A second tap at that same
  // position must cancel, never confirm a destructive action in the new modal.
  lv_obj_set_pos(confirm,0,164);
  c::buttonBackground(confirm,ui::theme::danger());
  poll();
}
void Recorder::rebuildList() {
  lv_obj_clean(list);
  for(size_t i=0;i<files.size();++i) {
    const size_t idx=files.size()-1-i;
    auto row=c::panel(list,240,layout::RowHeight);
    auto name=files[idx].name;
    // Generated names share a long date prefix. Keep the distinguishing time
    // and suffix visible on a second line instead of ellipsizing them away.
    if(name.size()==30 && name[4]=='-' && name[7]=='-' && name[10]=='_' &&
       name.compare(26,4,".wav")==0) {name.resize(26);name[10]='\n';}
    auto file=c::button(row,name.c_str(),[](lv_event_t *e) {
      auto self=static_cast<Recorder *>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
      self->play(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    },reinterpret_cast<void *>(idx),layout::FileWidth,layout::RowHeight);
    lv_obj_set_user_data(file,this);
    auto label=lv_obj_get_child(file,0);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);
    lv_obj_set_width(label,layout::FileWidth-8);
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_DOTS);
    auto remove=c::button(row,LV_SYMBOL_TRASH,[](lv_event_t *e) {
      auto self=static_cast<Recorder *>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
      self->askDelete(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    },reinterpret_cast<void *>(idx),layout::DeleteWidth,layout::RowHeight);
    iconSize(remove);
    lv_obj_set_user_data(remove,this);lv_obj_set_pos(remove,202,0);
  }
}
void Recorder::click(lv_event_t *e) {
  auto self=static_cast<Recorder *>(lv_event_get_user_data(e));
  auto target=lv_event_get_target_obj(e);
  if(lv_obj_has_state(target,LV_STATE_DISABLED)||self->dialog||self->deleting||!self->pendingDeletePath.empty())return;
  if(target==self->record) {
    if(!self->job&&!self->listing) {
      self->job=core::recorder::start(true);
      if(!self->job)c::setText(self->status,"音频任务忙，请稍后重试");
    } else if(self->job&&!self->job->recording&&!self->job->stop.load()) {
      self->recordAfterPlayback=true;
      self->job->setScrub(0);self->job->requestStop();
    } else if(self->job&&self->job->recording&&!self->job->stop.load())
      self->job->setPause(!self->job->pause.load());
  } else if(target==self->pause&&self->job&&!self->job->recording&&!self->job->stop.load()) {
    self->job->setScrub(0);
    self->job->setPause(!self->job->pause.load());
  } else if(target==self->stop&&self->job) {
    self->recordAfterPlayback=false;self->job->setScrub(0);
    self->job->requestStop();
  }
  self->poll();
}
void Recorder::seek(lv_event_t *e) {
  auto self=static_cast<Recorder *>(lv_event_get_user_data(e));
  const auto code=lv_event_get_code(e);
  auto target=lv_event_get_target_obj(e);
  // Release also runs after EOF has destroyed the Job or disabled the button.
  if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST) {
    if(self->job)self->job->setScrub(0);
    lv_obj_remove_state(target,static_cast<lv_state_t>(LV_STATE_PRESSED|LV_STATE_CHECKED));
    return;
  }
  if(!self->job||self->job->recording||self->job->done.load()||self->dialog)return;
  if(code==LV_EVENT_PRESSED&&!lv_obj_has_state(target,LV_STATE_DISABLED)&&!self->job->stop.load())
    self->job->setScrub(target==self->rewind?-1:1);
}
void Recorder::clearTransportPress() {
  for(auto button:{rewind,pause,forward})if(button)
    lv_obj_remove_state(button,static_cast<lv_state_t>(LV_STATE_PRESSED|LV_STATE_CHECKED));
}
void Recorder::tick(lv_timer_t *t) {static_cast<Recorder *>(lv_timer_get_user_data(t))->poll();}
void Recorder::poll() {
  if(job&&!job->done.load()&&!job->stop.load()&&(!job->pause.load()||job->scrub.load())&&
     sys::Power::isScreenOn()&&!sys::Power::sleepPending())lv_display_trigger_activity(nullptr);
  if(deleting&&deleting->done.load(std::memory_order_acquire)) {
    const int error=deleting->error;deleting.reset();
    noticeUntil=lv_tick_get()+3000;
    c::setText(status,error==ESTALE?"文件已变化，未删除，请重新选择":
                      error==EBUSY?"SD卡正忙，未删除，请稍后重试":
                      error?"删除失败，请检查SD卡":"录音已删除");
    if(core::sd::state()==core::sd::State::Available) refresh();
  }
  if(listing&&listing->done.load(std::memory_order_acquire)) {
    if(listing->error) {
      // A transient read failure is not an empty directory. Preserve the last
      // visible list but disallow stale actions until a successful refresh.
      filesGeneration=0;c::setText(status,core::storageError(listing->error));
    } else {
      filesGeneration=listing->cardGeneration;files=std::move(listing->entries);
      if(lv_tick_get()>=noticeUntil)c::setText(status,listing->truncated?"仅列出部分录音，其余请用文件管理":
                                                 files.empty()?"暂无录音，点击录音开始":"开始录音 / 点击列表播放");
      rebuildList();
    }
    listing.reset();
  }
  if(job) {
    const uint32_t bytes=job->bytes.load();
    const uint32_t seconds=bytes/core::wav::ByteRate;
    char time[32];std::snprintf(time,sizeof(time),"%02lu:%02lu:%02lu",
      (unsigned long)(seconds/3600),(unsigned long)(seconds/60%60),(unsigned long)(seconds%60));
    c::setText(elapsed,time);
    if(job->recording) {
      lv_bar_set_value(meter,std::min(job->peak.load(),32768u)*MeterMax/32768,LV_ANIM_OFF);
    } else {
      // Use the source position actually played, including reverse/2x playback.
      // Pausing keeps this position; WAV opening has no published length yet.
      const uint32_t length=job->length.load();
      const int progress=length?uint64_t(std::min(bytes,length))*MeterMax/length:0;
      lv_bar_set_value(meter,progress,LV_ANIM_OFF);
    }
    if(job->done.load(std::memory_order_acquire)) {
      const std::string result=job->ok?(job->recording?"录音已保存":"播放已结束"):job->error;
      job->setScrub(0);clearTransportPress();
      job.reset();noticeUntil=lv_tick_get()+3000;
      if(!recordAfterPlayback&&pendingDeletePath.empty()&&core::sd::state()==core::sd::State::Available) refresh();
      c::setText(status,result.c_str());
      lv_bar_set_value(meter,0,LV_ANIM_OFF);
    } else {
      const auto state=job->state.load();
      c::setText(status,job->stop.load()?(job->recording?"正在结束并保存":"正在结束播放"):
        job->scrub.load()<0?"2倍快退 / 松开恢复":job->scrub.load()>0?"2倍快进 / 松开恢复":
        state==core::recorder::State::Paused?(job->recording?"录音暂停 / 点击继续":"播放暂停 / 点击继续"):
        state==core::recorder::State::Recording?"正在录音 / 点击暂停":
        state==core::recorder::State::Playing?"正在播放 / 按住快进快退":
        state==core::recorder::State::Saving?"正在保存":"正在准备音频");
    }
  }
  // Wait for the old worker to close the file/codec and publish idle. A new
  // recording never shares its handles or receives old completion callbacks.
  if(!pendingDeletePath.empty()&&!job&&core::recorder::idle()) {
    deleting=core::deleteRecording(pendingDeletePath,pendingDeleteGeneration,pendingDeleteIdentity);
    pendingDeletePath.clear();pendingDeleteGeneration=0;playPath.clear();
    pendingDeleteIdentity={};
    c::setText(status,deleting?"正在删除录音":"删除启动失败，请重试");
    noticeUntil=lv_tick_get()+3000;
  }
  if(recordAfterPlayback&&!job&&core::recorder::idle()) {
    recordAfterPlayback=false;
    if(sys::Power::isScreenOn()&&!sys::Power::sleepPending()) {
      job=core::recorder::start(true);
      c::setText(status,job?"正在准备录音":"录音启动失败，请重试");
    }
  }
  updateMeter();
  const auto state=job?job->state.load():core::recorder::State::Finished;
  const bool ready=state==core::recorder::State::Recording||state==core::recorder::State::Playing||state==core::recorder::State::Paused;
  const auto enabled=layout::controls(bool(job),job&&job->recording,ready,
                                     job&&job->stop.load(),dialog||deleting||listing||recordAfterPlayback||!pendingDeletePath.empty());
  c::disabled(record,!enabled.record);c::disabled(stop,!enabled.stop);
  for(auto button:{rewind,pause,forward})c::disabled(button,!enabled.transport);
  if(!enabled.transport)clearTransportPress();
  else if(job->scrub.load()==0)for(auto button:{rewind,forward})
    lv_obj_remove_state(button,static_cast<lv_state_t>(LV_STATE_PRESSED|LV_STATE_CHECKED));
  c::disabled(back,!enabled.navigate);c::disabled(refreshButton,!enabled.files);
  const bool recordingNow=job&&job->recording&&!job->pause.load();
  if(recordingNow) {
    lv_obj_set_hidden(recordDot,true);lv_obj_set_hidden(recordPause,false);
  } else {
    lv_obj_set_hidden(recordDot,false);lv_obj_set_hidden(recordPause,true);
  }
  c::setText(lv_obj_get_child(pause,0),job&&job->pause.load()?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE);
  for(uint32_t i=0;i<lv_obj_get_child_count(list);++i) {
    auto row=lv_obj_get_child(list,i);
    c::disabled(lv_obj_get_child(row,0),!enabled.files||!filesGeneration);
    const size_t idx=files.size()-1-i;
    c::disabled(lv_obj_get_child(row,1),!filesGeneration||!files[idx].identity.valid||!layout::canDelete(enabled,bool(job),files[idx].path==playPath));
  }
}
bool Recorder::onBack() {if(!dialog)return false;closeDelete();poll();return true;}
void Recorder::onHide() {
  clearTransportPress();
  recordAfterPlayback=false;
  pendingDeletePath.clear();pendingDeleteGeneration=0;playPath.clear();
  pendingDeleteIdentity={};
  if(timer)lv_timer_pause(timer);
  if(job){job->setScrub(0);job->requestStop();}
  if(listing)listing->cancelled.store(true);
  if(deleting)deleting->cancelled.store(true);
  closeDelete();
}
void Recorder::onDestroy() {
  onHide();if(timer)lv_timer_delete(timer);timer=nullptr;
  job.reset();listing.reset();deleting.reset();files.clear();root=nullptr;
}
} // namespace apps
