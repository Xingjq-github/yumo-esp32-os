"""Recorder source contracts; no device SD, real recordings or serial are touched."""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/recorder-ui-dsp-20260919-1700'
def read(p):return (ROOT/p).read_text(encoding='utf-8-sig')
def compact(p):return re.sub(r'\s+','',read(p))
class RecorderUiDspContracts(unittest.TestCase):
 def test_current_scope(self):
  before=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
  allowed={'main/CMakeLists.txt','main/main.cpp','main/apps/settings_system.cpp','main/apps/recorder.cpp','main/apps/recorder.hpp','main/core/audio_recorder.cpp','main/core/audio_recorder.hpp','main/core/dual_mic_input.hpp','main/core/recording_level.hpp','main/core/storage_browser.cpp','main/core/storage_browser.hpp'}
  changed=set()
  for path,old in before.items():
   self.assertTrue((ROOT/path).is_file(),path)
   if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=old['sha256']:changed.add(path)
  self.assertEqual(changed,allowed)
 def test_header_reuses_notice_and_meter_tracks_actual_time_width(self):
  s=compact('main/apps/recorder.cpp')
  self.assertNotIn('c::label(header,"录音机"',s)
  self.assertIn('status=c::label(header,"开始录音/点击列表播放",153)',s)
  self.assertIn('lv_obj_get_x(elapsed)+lv_obj_get_width(elapsed)+layout::MeterGap',s)
  self.assertIn('lv_obj_set_size(meter,std::max(1,236-x),12)',s)
  self.assertIn('MeterGap=5',compact('main/ui/recorder_layout.hpp'))
 def test_record_pause_resume_share_original_large_button(self):
  s=compact('main/apps/recorder.cpp')
  self.assertIn('this,layout::Record.w,layout::Record.h',s);self.assertIn('this,121,40',s)
  section=s.split('if(target==self->record)',1)[1].split('elseif(target==self->pause',1)[0]
  self.assertIn('core::recorder::start(true)',section)
  self.assertIn('self->job->recording',section)
  self.assertIn('self->job->setPause(!self->job->pause.load())',section)
 def test_playback_exposes_transport_record_back_and_only_current_file_delete(self):
  s=compact('main/apps/recorder.cpp')
  for token in ('layout::controls(','c::disabled(record,!enabled.record)','c::disabled(stop,!enabled.stop)',
                'c::disabled(back,!enabled.navigate)','c::disabled(refreshButton,!enabled.files)',
                'c::disabled(lv_obj_get_child(row,0),!enabled.files||!filesGeneration)',
                'layout::canDelete(enabled,bool(job),files[idx].path==playPath)'):
   self.assertIn(token,s)
  self.assertIn('return{!ending,!ending,ready&&!ending,true,false,!ending}',compact('main/ui/recorder_layout.hpp'))
 def test_delete_confirmation_stable_target_and_cancel_no_write(self):
  s=compact('main/apps/recorder.cpp')
  self.assertIn('deletePath=files[index].path',s)
  self.assertEqual(s.count('core::deleteRecording('),1)
  cancel=s.split('autocancel=',1)[1].split('autoconfirm=',1)[0]
  self.assertIn('closeDelete()',cancel);self.assertNotIn('deleteRecording',cancel)
  self.assertIn('是否删除这条录音？',s);self.assertIn('删除后无法恢复',s)
  self.assertIn('LV_SYMBOL_TRASH',s)
  self.assertIn('if(job&&(job->recording||job->stop.load()||files[index].path!=playPath))return',s)
  self.assertIn('self->pendingDeletePath=self->deletePath',s)
  self.assertIn('self->pendingDeleteGeneration=self->filesGeneration',s)
  self.assertIn('if(!pendingDeletePath.empty()&&!job&&core::recorder::idle())',s)
  self.assertIn('core::deleteRecording(pendingDeletePath,pendingDeleteGeneration,pendingDeleteIdentity)',s)
  self.assertIn('deleteIdentity=files[index].identity',s)
  self.assertIn('self->pendingDeleteIdentity=self->deleteIdentity',s)
  self.assertIn('pendingDeletePath.clear()',s.split('voidRecorder::onHide()',1)[1])
 def test_delete_worker_is_constrained_cancellable_and_tracked(self):
  s=compact('main/core/storage_browser.cpp')
  block=s.split('if(job.removeRecording){',1)[1].split('if(job.text)',1)[0]
  for token in ('sd::child(job.path,sd::Recordings,".wav")','S_ISREG(st.st_mode)',
                'job.cancelled.load()||sys::Power::sleepPending()',
                'std::remove(job.path.c_str())'):
   self.assertIn(token,block)
  self.assertLess(block.index('S_ISREG'),block.index('std::remove'))
  self.assertEqual(s.count('std::remove('),1)
  self.assertIn('start(path,false,false,0,true,cardGeneration,identity)',s)
  self.assertIn('!card.claimMutation()',s)
  self.assertIn('job.deleteIdentity.size!=uint64_t(st.st_size)',block)
  self.assertIn('job.deleteIdentity.modified!=int64_t(st.st_mtime)',block)
  self.assertIn('job.error=ESTALE',block)
  self.assertIn('--active',s);self.assertIn('job->done.store(true,std::memory_order_release)',s)
  self.assertIn('!core::storageIdle()',read('main/core/power_mgr.cpp'))
 def test_scrub_hold_release_and_pwr_reset(self):
  ui=compact('main/apps/recorder.cpp');core=compact('main/core/audio_recorder.cpp')
  for token in ('LV_EVENT_PRESSED','LV_EVENT_RELEASED','LV_EVENT_PRESS_LOST','job->setScrub(0)','job->recording'):
   self.assertIn(token,ui)
  self.assertIn('job.pause.store(true);job.scrub.store(0)',core)
  self.assertIn('player->next(direction,read)',core)
  self.assertIn('job.stop.load()||job.scrub.load()!=direction||(job.pause.load()&&direction==0)',core)
  self.assertIn('static_cast<long>(position)',core)
  self.assertIn('clearTransportPress()',ui)
  self.assertLess(ui.index('if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)'),ui.index('if(!self->job||self->job->recording'))
 def test_transport_never_changes_codec_rate_or_touches_file_on_gui(self):
  core=compact('main/core/audio_recorder.cpp');ui=read('main/apps/recorder.cpp')
  self.assertIn('openStream(record,wav::Rate)',core)
  player=compact('main/core/recorder_playback.hpp')
  for token in ('esp_ae_sonic_set_speed(sonic_,2.0f)','esp_ae_sonic_set_pitch(sonic_,1.0f)',
                'std::reverse(input_,input_+frames)','reverse_.process(input_,frames)',
                '~Playback(){close();}','MaxPadding=8192'):
   self.assertIn(token,player)
  self.assertNotRegex(ui,r'\b(?:fseek|fread|fwrite|unlink|fopen)\s*\(')
 def test_noise_suppression_only_records_and_no_double_agc(self):
  s=compact('main/core/audio_recorder.cpp');ns=compact('components/recorder_dsp/recorder_denoise.c')
  self.assertLess(s.index('if(!denoise.open())'),s.index('if(!stream.open(true))'))
  self.assertEqual(s.count('denoise.process('),1)
  for token in ('attenuation=-12','SPEEX_PREPROCESS_SET_DENOISE,&on','SPEEX_PREPROCESS_SET_AGC,&off','SPEEX_PREPROCESS_GET_PROB'):
   self.assertIn(token,ns)
  self.assertIn('clean.frames,denoise.voice()',s)
  self.assertIn('allowBoost&&peak>=384',compact('main/core/recording_level.hpp'))
 def test_denoise_owns_one_bounded_arena_and_releases_it(self):
  s=compact('components/recorder_dsp/recorder_denoise.c')
  for token in ('heap_caps_aligned_calloc(16,1,sizeof(*ctx),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)',
                'RECORDER_NS_ARENA-allocating->used','setjmp(ctx->failure)',
                'longjmp(allocating->failure,1)','free(ctx)'):
   self.assertIn(token,s)
  header=compact('components/recorder_dsp/include/recorder_denoise.h')
  self.assertIn('RECORDER_NS_FRAME256',header);self.assertIn('RECORDER_NS_ARENA(64*1024)',header)
  self.assertIn('~RecordingDenoise(){recorder_denoise_destroy(context_);}',compact('main/core/recording_denoise.hpp'))
 def test_latency_is_drained_once_at_pause_and_stop(self):
  s=compact('main/core/audio_recorder.cpp')
  self.assertIn('play(job,file,stream,limit);return;',s)
  self.assertIn('if(!wasPaused){if(!flushTail())break;',s)
  self.assertIn('if(job.error.empty())(void)flushTail()',s)
  self.assertIn('outputBytes=clean.frames*sizeof(int16_t)',s)
  self.assertIn('std::min<size_t>(tail.frames,(limit-total)/sizeof(int16_t))',s)
  wrapper=compact('main/core/recording_denoise.hpp')
  for token in ('latency_.acceptFrame()','latency_.flush()','latency_.reset()','frames%RECORDER_NS_FRAME'):
   self.assertIn(token,wrapper)
 def test_pair_fusion_requires_positive_correlation_and_no_clipping(self):
  s=compact('main/core/dual_mic_input.hpp')
  for token in ('coherent=cross>0','!stats[0].clipped&&!stats[1].clipped',
                'energy[0]<=2*energy[1]','energy[1]<=2*energy[0]',
                'cross*cross*16>=energy[0]*energy[1]*9','elsemix_=0'):
   self.assertIn(token,s)
 def test_upstream_is_pinned_with_only_documented_echo_patch(self):
  hashes=json.loads((RUN/'speex-upstream-files.json').read_text(encoding='utf-8'))
  changed=[]
  for path,expected in hashes.items():
   if hashlib.sha256((ROOT/'components/recorder_dsp/upstream'/path).read_bytes()).hexdigest()!=expected:changed.append(path)
  self.assertEqual(changed,['libspeexdsp/preprocess.c'])
  self.assertIn('#ifndef RECORDER_NO_ECHO',read('components/recorder_dsp/upstream/libspeexdsp/preprocess.c'))
  self.assertIn('SpeexDSP-1.2.1',read('components/recorder_dsp/PORT.md'))
  self.assertIn('Redistribution and use',read('components/recorder_dsp/upstream/COPYING'))
 def test_model_checks_real_layout_transport_latency_and_dsp(self):
  s=read('../tooling/firmware/recorder_controls_model_test.cpp')
  for test in ('layoutFits','controlsLocked','seekingIsTwoTimesAndBounded','latencyPreservesSamplesAndPause',
               'coherentPairReducesOpposedNoise','speechGateDoesNotDeleteSamplesOrBoostNoise'):
   self.assertIn('static_assert('+test+'())',s)
if __name__=='__main__':unittest.main(verbosity=2)
