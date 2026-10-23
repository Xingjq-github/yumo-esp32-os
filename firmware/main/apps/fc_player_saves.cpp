#include "fc_player.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include <cstdio>
#include <ctime>
namespace apps {
namespace c=ui::compact;
namespace {
void slotText(const fc_save_slot_t &slot,unsigned index,char *date,size_t dateSize,char *duration,size_t durationSize) {
  const bool automatic=index==FC_SAVE_AUTO_SLOT;
  if(!slot.valid) {
    if(automatic)std::snprintf(date,dateSize,"自动存档");
    else std::snprintf(date,dateSize,"%u",index+1);
    std::snprintf(duration,durationSize,"%s",automatic?
                  (slot.exists?"存档无效，等待自动保存":"游戏满1分钟后保存"):
                  (slot.exists?"存档无效，可覆盖":"空白存档"));
    return;
  }
  const time_t stamp=static_cast<time_t>(slot.timestamp);
  struct tm local{};
  char time[32]="----/--/-- --:--:--";
  if(stamp>0 && localtime_r(&stamp,&local)) std::strftime(time,sizeof(time),"%Y/%m/%d %H:%M:%S",&local);
  if(automatic)std::snprintf(date,dateSize,"自动 %s",time);
  else std::snprintf(date,dateSize,"%u  %s",index+1,time);
  const uint64_t seconds=slot.rate?slot.frames/slot.rate:0;
  std::snprintf(duration,durationSize,"时长 %llu:%02u:%02u%s",
                static_cast<unsigned long long>(seconds/3600),unsigned(seconds/60%60),unsigned(seconds%60),automatic?" · 只读":"");
}
}
void FcPlayer::showSaves() {
  if(!job || !job->ready.load(std::memory_order_acquire) || job->stop.load() || savePanel || saveBusy ||
     job->saves.stage.load()!=core::fc::SaveStage::Idle) return;
  showMenu();saveSelection=0;saveListReady=false;saveBusy=false;
  saveSlots={};saveGeneration=0;
  savePanel=c::panel(root(),240,240);
  lv_obj_set_gesture_bubble(savePanel,false);
  saveBack=c::button(savePanel,LV_SYMBOL_LEFT,[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->closeSaves();
  },this,36,36);
  lv_obj_set_pos(saveBack,0,0);
  saveTitle=c::label(savePanel,"存档/读取",156);lv_obj_set_pos(saveTitle,42,9);
  saveRefresh=c::button(savePanel,LV_SYMBOL_REFRESH,[](lv_event_t *e) {
    static_cast<FcPlayer *>(lv_event_get_user_data(e))->submitSave(core::fc::SaveAction::Browse);
  },this,36,36);lv_obj_set_pos(saveRefresh,204,0);
  saveList=c::panel(savePanel,240,158,true);lv_obj_set_pos(saveList,0,38);
  lv_obj_set_flex_flow(saveList,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(saveList,2,0);
  for(unsigned order=0;order<FC_SAVE_SLOTS;++order) {
    const unsigned i=order==0?unsigned(FC_SAVE_AUTO_SLOT):order-1;
    auto row=c::button(saveList,"",[](lv_event_t *e) {
      auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
      if(self->saveBusy || self->saveDialog || !self->saveListReady) return;
      self->saveSelection=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
      self->updateSaves();
    },this,236,38);
    lv_obj_set_user_data(row,reinterpret_cast<void *>(uintptr_t(i)));
    lv_obj_set_style_pad_all(row,0,0);
    lv_obj_set_style_bg_color(row,ui::theme::accent(),LV_STATE_CHECKED);
    saveRows[i]=row;
    saveDates[i]=c::label(row,"",224);lv_obj_set_pos(saveDates[i],6,i==FC_SAVE_AUTO_SLOT?0:3);
    lv_obj_set_style_text_font(saveDates[i],i==FC_SAVE_AUTO_SLOT?ui::theme::fontMenu():&lv_font_montserrat_12,0);
    saveTimes[i]=c::label(row,"",224);lv_obj_set_pos(saveTimes[i],6,18);
    lv_obj_set_style_text_font(saveTimes[i],ui::theme::fontMenu(),0);
  }
  saveWrite=c::button(savePanel,"存档",[](lv_event_t *e) {
    auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
    if(self->saveBusy || !self->saveListReady || self->saveSelection>=FC_SAVE_MANUAL_SLOTS) return;
    if(self->saveSlots[self->saveSelection].exists) self->confirmSave();
    else self->submitSave(core::fc::SaveAction::Save);
  },this,118,40);lv_obj_set_pos(saveWrite,0,200);
  saveRead=c::button(savePanel,"读档",[](lv_event_t *e) {
    auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
    if(self->saveListReady && self->saveSlots[self->saveSelection].valid)
      self->submitSave(core::fc::SaveAction::Load);
  },this,118,40);lv_obj_set_pos(saveRead,122,200);
  submitSave(core::fc::SaveAction::Browse);
}
void FcPlayer::closeSaves() {
  if(saveBusy && job)job->saves.cancelled.store(true);
  if(saveDialog) { lv_obj_delete(saveDialog);saveDialog=nullptr;return; }
  if(savePanel) lv_obj_delete(savePanel);
  savePanel=saveTitle=saveList=saveBack=saveRefresh=saveWrite=saveRead=nullptr;
  saveRows={};saveDates={};saveTimes={};saveListReady=false;
  clearInput(); // held Space/Esc must not leak into the resumed game/menu
}
void FcPlayer::updateSaves() {
  if(!savePanel) return;
  for(unsigned i=0;i<FC_SAVE_SLOTS;++i) {
    char date[48],duration[64];slotText(saveSlots[i],i,date,sizeof(date),duration,sizeof(duration));
    c::setText(saveDates[i],date);c::setText(saveTimes[i],duration);
    c::checked(saveRows[i],i==saveSelection);
    c::disabled(saveRows[i],saveBusy || !saveListReady);
  }
  c::disabled(saveBack,false);c::disabled(saveRefresh,saveBusy);
  c::disabled(saveWrite,saveBusy || !saveListReady || saveSelection>=FC_SAVE_MANUAL_SLOTS);
  c::disabled(saveRead,saveBusy || !saveListReady || !saveSlots[saveSelection].valid);
}
void FcPlayer::saveNotice(const char *message,bool confirm) {
  if(saveDialog) lv_obj_delete(saveDialog);
  saveDialog=c::panel(savePanel,240,240);
  lv_obj_set_gesture_bubble(saveDialog,false);
  auto text=c::label(saveDialog,message,224);lv_obj_set_pos(text,8,36);
  lv_obj_set_height(text,134);lv_label_set_long_mode(text,LV_LABEL_LONG_MODE_WRAP);
  auto cancel=c::button(saveDialog,confirm?"取消":"返回",[](lv_event_t *e) {
    auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
    if(self->saveDialog) lv_obj_delete(self->saveDialog);
    self->saveDialog=nullptr;self->clearInput();
  },this,confirm?116:224,42);lv_obj_set_pos(cancel,8,184);
  if(confirm) {
    auto yes=c::button(saveDialog,"覆盖存档",[](lv_event_t *e) {
      auto self=static_cast<FcPlayer *>(lv_event_get_user_data(e));
      lv_obj_delete(self->saveDialog);self->saveDialog=nullptr;
      self->submitSave(core::fc::SaveAction::Save);
    },this,116,42);lv_obj_set_pos(yes,124,184);
  }
}
void FcPlayer::confirmSave() {
  if(saveBusy || saveDialog || !saveListReady || saveSelection>=FC_SAVE_MANUAL_SLOTS) return;
  char date[48],duration[64],message[200];
  slotText(saveSlots[saveSelection],saveSelection,date,sizeof(date),duration,sizeof(duration));
  std::snprintf(message,sizeof(message),"覆盖存档 %u？\n%s\n%s\n原进度将被替换。",saveSelection+1,date,duration);
  saveNotice(message,true);
}
void FcPlayer::submitSave(core::fc::SaveAction action) {
  if(!job || saveBusy || saveDialog) return;
  if(action==core::fc::SaveAction::Save && saveSelection>=FC_SAVE_MANUAL_SLOTS)return;
  if(action!=core::fc::SaveAction::Browse && !saveListReady) return;
  if(!core::fc::requestSave(*job,action,saveSelection,&saveSlots[saveSelection],saveGeneration)) return;
  saveBusy=true;
  c::setText(saveTitle,action==core::fc::SaveAction::Browse?"读取存档列表":
                       action==core::fc::SaveAction::Save?"正在存档":"正在读档");
  updateSaves();
}
void FcPlayer::pollSaves() {
  if(!job || !saveBusy || job->saves.stage.load(std::memory_order_acquire)!=core::fc::SaveStage::Complete) return;
  auto &s=job->saves;
  const bool loaded=s.success && s.action==core::fc::SaveAction::Load;
  saveBusy=false;
  if(!savePanel) { // Cancelled/closed UI must never dereference deleted widgets.
    s.stage.store(core::fc::SaveStage::Idle,std::memory_order_release);return;
  }
  if(s.success) { saveSlots=s.slots;saveGeneration=s.generation;saveListReady=true; }
  else saveListReady=false; // require an explicit refresh after changed media/IO failure
  c::setText(saveTitle,s.success?(s.action==core::fc::SaveAction::Save?"存档成功":"存档/读取"):"操作失败");
  if(loaded) { pauseNotice=s.message;pauseNoticeAt=lv_tick_get(); }
  else if(!s.success) saveNotice(s.message,false);
  s.stage.store(core::fc::SaveStage::Idle,std::memory_order_release);
  if(loaded) { closeSaves();showMenu(); }
  else updateSaves();
}
} // namespace apps
