#include "lvgl.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/control_states.h"
#define private public
#include "apps/fc_player.hpp"
#undef private
static unsigned checks=0,requests=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace ui {void TextInput::hide(){}void TextInput::show(lv_obj_t *){}bool TextInput::visible(){return false;}void TextInput::bindField(lv_obj_t *){}}
namespace core::fc {
Job::~Job()=default;
bool requestSave(Job &j,SaveAction a,unsigned slot,const fc_save_slot_t *,uint64_t){
 CHECK(j.paused && j.saves.stage==SaveStage::Idle);++requests;j.saves.action=a;j.saves.slot=slot;
 j.saves.stage=SaveStage::Pending;return true;
}
}
namespace apps {
void FcPlayer::onCreate(lv_obj_t *){}void FcPlayer::onHide(){}void FcPlayer::onDestroy(){}
bool FcPlayer::onBack(){return false;}void FcPlayer::clearInput(){}
void FcPlayer::showMenu(){job->paused=true;}
}
#include "compact_ui.inc"
static tm *localtime_r(const time_t *n,tm *t){return localtime_s(t,n)==0?t:nullptr;}
#include "fc_player_saves.inc"
static uint8_t framebuffer[240*240*2];
static void advance(){lv_tick_inc(300);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static void click(lv_obj_t *o){lv_obj_send_event(o,LV_EVENT_CLICKED,nullptr);advance();}
static void complete(apps::FcPlayer &a){auto &s=a.job->saves;s.success=true;s.generation=1;s.stage=core::fc::SaveStage::Complete;a.pollSaves();advance();}
static size_t memory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
int main(){
 lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 for(int theme:{4,5,6,8,10}){size_t baseline=0;for(int cycle=0;cycle<3;++cycle){
  ui::theme::init(theme);auto r=ui::compact::panel(lv_screen_active(),240,240);apps::FcPlayer a;a.setRoot(r);
  a.job=std::make_shared<core::fc::Job>();a.job->ready=true;a.showSaves();complete(a);
  CHECK(a.saveSelection==0);CHECK(lv_obj_get_child_count(a.saveList)==4);
  CHECK(lv_obj_get_child(a.saveList,0)==a.saveRows[FC_SAVE_AUTO_SLOT]);
  for(unsigned i=0;i<FC_SAVE_SLOTS;++i){
   lv_area_t area{};lv_obj_get_coords(a.saveRows[i],&area);CHECK(area.y1>=38 && area.y2<196);
   lv_area_t date{},duration{};lv_obj_get_coords(a.saveDates[i],&date);lv_obj_get_coords(a.saveTimes[i],&duration);
   CHECK(date.y2<duration.y1 && duration.y2<=area.y2);
  }
  click(a.saveRows[FC_SAVE_AUTO_SLOT]);CHECK(lv_obj_has_state(a.saveWrite,LV_STATE_DISABLED));CHECK(lv_obj_has_state(a.saveRead,LV_STATE_DISABLED));
  auto n=requests;click(a.saveWrite);a.submitSave(core::fc::SaveAction::Save);a.confirmSave();CHECK(requests==n && !a.saveDialog);
  auto &s=a.job->saves.slots[FC_SAVE_AUTO_SLOT];s.valid=true;s.exists=true;s.timestamp=1790755200;s.frames=3600;s.rate=60;
  a.saveSlots=a.job->saves.slots;a.updateSaves();advance();CHECK(!lv_obj_has_state(a.saveRead,LV_STATE_DISABLED));
  CHECK(lv_obj_has_state(a.saveWrite,LV_STATE_DISABLED));click(a.saveRead);CHECK(a.job->saves.action==core::fc::SaveAction::Load);
  complete(a);CHECK(!a.savePanel && a.job->paused);
  a.showSaves();complete(a);click(a.saveRows[2]);click(a.saveWrite);CHECK(a.saveBusy && a.job->saves.slot==2);complete(a);
  a.saveSlots[2].exists=true;click(a.saveWrite);CHECK(a.saveDialog && !a.saveBusy);a.closeSaves();CHECK(!a.saveDialog && a.savePanel);
  a.closeSaves();CHECK(!a.savePanel);
  a.showSaves();CHECK(a.saveBusy && !lv_obj_has_state(a.saveBack,LV_STATE_DISABLED));
  click(a.saveBack);CHECK(!a.savePanel && a.saveBusy && a.job->saves.cancelled);
  a.showSaves();CHECK(!a.savePanel); // old completion cannot target a new dialog
  a.job->saves.success=false;a.job->saves.stage=core::fc::SaveStage::Complete;
  a.pollSaves();CHECK(!a.saveBusy && !a.saveDialog && a.job->saves.stage==core::fc::SaveStage::Idle);
  a.job.reset();lv_obj_delete(r);advance();
  if(!cycle)baseline=memory();else CHECK(memory()==baseline);
 }}std::printf("%u checks: real LVGL auto-first/three-manual layout, read-only selection, load-to-menu, overwrite confirmation and release across five themes.\n",checks);
}
