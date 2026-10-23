"""Current recorder icon/FC contracts; source and synthetic data only."""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/recorder-icons-fc-20260919-1822'
def read(p):return (ROOT/p).read_text(encoding='utf-8-sig')
def compact(p):return re.sub(r'\s+','',read(p))
ALLOWED={'components/nofrendo/PORT.md','main/main.cpp','main/apps/settings_system.cpp','main/apps/recorder.cpp','main/apps/recorder.hpp','main/ui/recorder_layout.hpp','main/core/fc_emulator.cpp','main/core/fc_emulator.hpp','main/apps/fc_player.cpp','main/apps/fc_player.hpp','components/nofrendo/fc_port.c','components/nofrendo/fc_port.h','components/nofrendo/nes/mem.c','components/nofrendo/nes/ppu.c','components/nofrendo/nes/apu.c'}
class RecorderFcContracts(unittest.TestCase):
 def test_scope(self):
  before=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files'];changed=set()
  for path,old in before.items():
   self.assertTrue((ROOT/path).is_file(),path)
   if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=old['sha256']:changed.add(path)
  self.assertEqual(changed,ALLOWED)
 def test_both_edges_align_and_transport_touch_sizes_are_kept(self):
  s=compact('main/ui/recorder_layout.hpp')
  for token in ('Record{0,64,118,77}','Stop{119,64,121,40}','List{0,142,240,98}','Record.y==Stop.y','Record.y+Record.h==Forward.y+Forward.h'):
   self.assertIn(token,s)
 def test_record_circle_has_no_font_dependency_and_no_touch_interception(self):
  s=compact('main/apps/recorder.cpp')
  for token in ('lv_obj_set_size(recordDot,28,28)','LV_RADIUS_CIRCLE','lv_obj_set_clickable(recordDot,false)','lv_obj_set_scrollable(recordDot,false)','recordPause=c::label(record,LV_SYMBOL_PAUSE)','lv_font_montserrat_24','recordingNow=job&&job->recording&&!job->pause.load()'):
   self.assertIn(token,s)
  self.assertNotIn('c::setText(lv_obj_get_child(record,0)',s)
 def test_transport_uses_large_icons_without_labels(self):
  s=read('main/apps/recorder.cpp');controls=s.split('record=c::button',1)[1].split('list=c::panel',1)[0]
  for word in (' 录音',' 暂停',' 继续',' 停止','2x'):self.assertNotIn(word,controls)
  for token in ('LV_SYMBOL_STOP','LV_SYMBOL_LEFT LV_SYMBOL_LEFT','LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT','iconSize(button)'):
   self.assertIn(token,controls)
  self.assertIn('lv_font_montserrat_24',s)
 def test_existing_audio_transport_is_frozen(self):
  before=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
  for path in ('main/core/audio_mgr.cpp','main/core/audio_recorder.cpp','main/core/dual_mic_input.hpp','main/core/recording_level.hpp','main/core/recording_denoise.hpp'):
   self.assertEqual(hashlib.sha256((ROOT/path).read_bytes()).hexdigest(),before[path]['sha256'])
 def test_every_game_tick_emulates_cpu_apu_regardless_of_video(self):
  s=compact('main/core/fc_emulator.cpp')
  self.assertIn('input_update(0,controls::neutralOpposites(pad))',s)
  self.assertIn('sys::Buttons::takeGameKeys(job.keySession)',s)
  self.assertIn('nes_emulate(draw);restoredFrame=false;',s)
  self.assertLess(s.index('nes_emulate(draw);'),s.index('if(draw)'))
  self.assertNotIn('if(draw)nes_emulate',s)
  self.assertEqual(s.count('nes_emulate('),1)
  self.assertIn('sys::Audio::writeStream(nes->apu->buffer,nes->apu->samples_per_frame*2)',s)
  self.assertIn('openStream(false,22050)',s)
 def test_one_mailbox_no_blocking_producer_and_no_gui_audio(self):
  worker=compact('main/core/fc_emulator.cpp');ui=compact('main/apps/fc_player.cpp')
  self.assertIn('std::try_to_lock',worker);self.assertIn('std::try_to_lock',ui)
  self.assertNotIn('lock_guard<std::mutex>lock(job.frameMutex)',worker)
  self.assertIn('!job.framePending.load(std::memory_order_acquire)',worker)
  self.assertIn('job.framePending.store(true,std::memory_order_release)',worker)
  self.assertIn('job->framePending.store(false,std::memory_order_release)',ui)
  self.assertIn('std::memcpy(pixels,job->frame,core::fc::Pixels*2)',ui)
  self.assertNotIn('Audio::',ui)
 def test_runtime_polling_and_no_always_one_tick_penalty(self):
  s=compact('main/apps/fc_player.cpp')
  self.assertIn('lv_timer_set_period(timer,100)',s)
  self.assertIn('lv_timer_set_period(timer,job->paused.load()?50:16)',s)
  worker=compact('main/core/fc_emulator.cpp')
  self.assertIn('timing.waitTicks(now,timing.finish(now),1000000/configTICK_RATE_HZ)',worker)
  self.assertIn('if(ticks)vTaskDelay(ticks);elsetaskYIELD()',worker)
  self.assertIn('if(now-deadline_>100000)',compact('main/core/fc_timing.hpp'))
 def test_pause_exit_input_and_power_contracts_remain(self):
  s=compact('main/core/fc_emulator.cpp')
  for token in ('job.stop.load()','sys::Power::sleepPending()','if(!sys::Power::isScreenOn())','job.buttons.store(0)','job.pressed.store(0)','sys::Power::WorkGuard','vTaskDeleteWithCaps(nullptr)'):
   self.assertIn(token,s)
  ui=compact('main/apps/fc_player.cpp')
  self.assertIn('ExternalKeyboard::capture(nullptr,nullptr)',ui)
  self.assertIn('heap_caps_free(pixels)',ui)
 def test_large_buffers_stay_in_psram_and_hot_allocations_fallback(self):
  s=compact('components/nofrendo/fc_port.c')
  for token in ('count>SIZE_MAX/size','bytes<=4096','heap_caps_get_free_size(caps)>=96*1024+bytes','MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT','returnfc_calloc(count,size)'):
   self.assertIn(token,s)
  self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',s)
  for name,kind in (('mem','mem_t'),('ppu','ppu_t'),('apu','apu_t')):
   source=compact('components/nofrendo/nes/'+name+'.c')
   self.assertIn('context=fc_calloc_hot(1,sizeof('+kind+'))',source)
   self.assertIn('free(context)',source)
  self.assertIn('free(ppu.nametab)',compact('components/nofrendo/nes/ppu.c'))
 def test_real_production_models_are_used(self):
  s=read('../tooling/firmware/fc_performance_model_test.cpp')
  for token in ('#include "core/fc_timing.hpp"','#include "core/fc_video.hpp"','static_assert(exactNativeRate(50))','static_assert(exactNativeRate(60))','static_assert(nativeViewportIsExact())','static_assert(bothPhasesSurvive(50))','static_assert(bothPhasesSurvive(60))'):
   self.assertIn(token,s)
if __name__=='__main__':unittest.main(verbosity=2)
