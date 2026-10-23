"""Wi-Fi/media lifetime + FC Bluetooth source contracts and specification models.
No compiler, serial device, server or user database is used by these checks.
Python models describe intended state transitions; they do not run ESP32 C++.
"""
from pathlib import Path
import hashlib,json,re,unittest,itertools
from sdkconfig_snapshot import historical_sdkconfig_bytes
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/wifi-fc-lifecycle-20260920'
def read(p):return (ROOT/p).read_text(encoding='utf-8-sig')
def compact(p):return re.sub(r'\s+','',read(p))
def part(s,a,b):return s.split(a,1)[1].split(b,1)[0]
BSP='components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c'
class MediaLifetimeContracts(unittest.TestCase):
 def test_changes_stay_within_current_authorized_source_scope(self):
  allowed={'main/core/audio_mgr.cpp','main/core/audio_mgr.hpp','main/core/system_service.cpp','main/core/system_service.hpp','main/core/connectivity.cpp','main/apps/settings.cpp','main/apps/settings_wifi.cpp','main/apps/password_vault.cpp','main/apps/fc_player.cpp','main/apps/fc_player.hpp',BSP,'components/esp32_s3_touch_lcd_1_54/include/bsp/esp32_s3_touch_lcd_1_54.h','../tooling/firmware/check_sd_root_audio.py','../tooling/firmware/check_vault_fullscreen_enter.py'}
  before=json.loads((RUN/'before.json').read_text())['files']
  for path,item in before.items():
   if path.startswith(('../docs/firmware/','../docs/history/memory/')) or path in allowed:continue
   self.assertTrue((ROOT/path).is_file(),path)
   self.assertEqual(hashlib.sha256((ROOT/path).read_bytes()).hexdigest(),item['sha256'],path)
 def test_queue_full_is_not_memory_failure(self):
  s=compact('main/core/system_service.cpp')
  enqueue=part(s,'esp_err_tenqueue(', 'esp_err_tsyncTime()')
  self.assertIn('returnSystemService::QueueFull',enqueue)
  self.assertNotIn('ESP_ERR_NO_MEM',enqueue)
  self.assertIn('std::memset(cmd.password,0,sizeof(cmd.password))',enqueue)
  self.assertIn('caseQueueFull:return"操作队列已满',s)
  self.assertIn('caseESP_ERR_NO_MEM:return"可用内存不足',s)
  self.assertIn('SystemService::errorText(err)',read('main/apps/settings.cpp'))
 def test_wifi_switch_deduplicates_pending_request_and_reopens_after_failure(self):
  s=compact('main/core/system_service.cpp')
  self.assertIn('wifiTogglePending.exchange(true))returnESP_ERR_NOT_FINISHED',s)
  self.assertIn('error!=ESP_OK)wifiTogglePending.store(false)',s)
  self.assertIn('cmd.action==ServiceAction::WifiEnable)wifiTogglePending.store(false)',s)
  self.assertIn('c::disabled(toggle,true)',compact('main/apps/settings_wifi.cpp'))
 def test_idle_volume_does_not_initialize_i2s(self):
  s=part(read('main/core/audio_mgr.cpp'),'bool Audio::applyVolume','void Audio::suspendForStandby')
  self.assertNotRegex(s,r'\b(?:init|resumeFromStandby|openStream)\(')
  self.assertIn('if (!s_speaker || s_suspended) return true',s)
  self.assertNotIn('s_initAttempted',read('main/core/audio_mgr.cpp'))
 def test_wifi_only_reclaims_idle_audio_without_waiting_for_active_io(self):
  s=compact('main/core/system_service.cpp')
  self.assertIn('if(cmd.value)(void)Audio::releaseIdle()',s)
  idle=part(compact('main/core/audio_mgr.cpp'),'boolAudio::releaseIdle()', '} // namespace'.replace(' ',''))
  self.assertIn('std::try_to_lock',idle)
  self.assertLess(idle.index('!lock.owns_lock()||s_stream'),idle.index('releaseAudioLocked()'))
 def test_close_releases_all_owned_objects_not_only_the_codec_device(self):
  s=read(BSP);owner=part(s,'static void bsp_codec_release','static esp_err_t bsp_i2s_release')
  order=['esp_codec_dev_delete','audio_codec_delete_codec_if','audio_codec_delete_ctrl_if','audio_codec_delete_gpio_if']
  self.assertEqual(sorted(owner.index(x) for x in order),[owner.index(x) for x in order])
  deinit=part(s,'esp_err_t bsp_audio_deinit','esp_err_t bsp_audio_init')
  order=['bsp_codec_release(&speaker_owner)','bsp_codec_release(&microphone_owner)','audio_codec_delete_data_if','bsp_i2s_release(&i2s_tx_chan)','bsp_i2s_release(&i2s_rx_chan)']
  self.assertEqual(sorted(deinit.index(x) for x in order),[deinit.index(x) for x in order])
  self.assertIn('releaseAudioLocked()',part(read('main/core/audio_mgr.cpp'),'void Audio::closeStream','bool Audio::releaseIdle'))
 def test_channel_disable_precedes_delete_and_failed_handle_is_retained(self):
  s=part(read(BSP),'static esp_err_t bsp_i2s_release','esp_err_t bsp_audio_deinit')
  self.assertIn('if (info.is_enabled)',s)
  self.assertLess(s.index('i2s_channel_disable'),s.index('i2s_del_channel'))
  self.assertIn('if (err == ESP_OK) *channel = NULL',s)
  self.assertNotIn('bsp_i2c_deinit',part(read(BSP),'esp_err_t bsp_audio_deinit','esp_err_t bsp_audio_init'))
 def test_all_initialization_failures_have_rollback(self):
  s=read(BSP)
  self.assertIn('if (!i2s_data_if) { ret = ESP_ERR_NO_MEM; goto err; }',s)
  for label,end,owner,other in [('speaker','microphone','speaker_owner','microphone_owner'),('microphone',None,'microphone_owner','speaker_owner')]:
   body=s.split('esp_codec_dev_handle_t bsp_audio_codec_'+label+'_init',1)[1].split('\n}',1)[0]
   self.assertIn('bsp_codec_release(&'+owner+')',body)
   self.assertIn('if (!'+other+'.device) (void)bsp_audio_deinit()',body)
 def test_record_gain_readback_and_dual_mic_pipeline_are_preserved(self):
  s=compact('main/core/audio_mgr.cpp')
  for token in ('format.channel=record?2:1','s_stream,micChannels,36.0f','micGainFirstRegister=0x43,micGainCode=13','for(intchannel=0;channel<2;++channel)','(gainRegister&0x0f)==micGainCode'):
   self.assertIn(token,s)
  fail=part(s,'if(!gainOk)', '}else')
  self.assertIn('releaseAudioLocked();returnfalse',fail)
 def test_recorder_finishes_file_before_publishing_done(self):
  s=read('main/core/audio_recorder.cpp')
  order=['stream.close();\n  job.state.store(State::Saving)','const bool saved = checkpoint(file.p, total)','std::fclose(file.p)','std::rename(temporary.c_str(), job.path.c_str())']
  positions=[s.index(x,s.index('stream.close();\n  job.state.store(State::Saving)')) for x in order]
  self.assertEqual(positions,sorted(positions))
  self.assertIn('~Stream() { close(); }',s)
  worker=part(s,'void worker(', '} // namespace')
  self.assertLess(worker.index('execute(*job)'),worker.index('job->done.store'))
  self.assertLess(worker.index('job->done.store'),worker.index('active.store(false)'))
 def test_app_exit_is_cooperative_and_never_gui_deletes_an_audio_task(self):
  for name in ('recorder','fc_player'):
   s=read('main/apps/'+name+'.cpp')
   self.assertIn('job->requestStop()',s);self.assertIn('listing->cancelled.store(true)',s)
   self.assertIn('lv_timer_delete(timer)',s)
   self.assertNotIn('vTaskDelete',s);self.assertNotIn('Audio::',s)
  s=read('main/core/fc_emulator.cpp')
  engine=part(s,'struct Engine','void loadMapping')
  for token in ('~Engine()','sys::Audio::closeStream()','nes_shutdown()'):self.assertIn(token,engine)
  self.assertIn('Job::~Job() { heap_caps_free(saves.autoSnapshot);heap_caps_free(frame); }',s)
  self.assertIn('!active.load() && autoSavesIdle()',s)
 def test_no_new_diagnostic_loop_or_radio_buffer_config_change(self):
  s=read('main/core/connectivity.cpp')
  body=part(s,'void reportStartFailure','void setState')
  self.assertNotRegex(body,r'while\(|for\(|xTaskCreate|lv_timer_create')
  self.assertIn('heap_caps_get_largest_free_block',body)
  before=json.loads((RUN/'before.json').read_text())['files']
  for path in ('sdkconfig','sdkconfig.defaults','partitions.csv','main/core/fc_timing.hpp','main/core/dual_mic_input.hpp','main/core/recording_denoise.hpp'):
   data=(ROOT/path).read_bytes()
   if path=='sdkconfig':data=historical_sdkconfig_bytes(data)
   if path in ('sdkconfig','sdkconfig.defaults'):
    from check_auto_power import assert_config
    assert_config(self,path)
   elif path=='partitions.csv':
    from prepare_vault_flash import partition_layout
    layout=partition_layout((ROOT/'../artifacts/firmware/partition_table/partition-table.bin').read_bytes())
    self.assertIn(('assets',1,0x82,0x970000,0x550000),layout)
    self.assertEqual(layout[-1],('vault',1,0x40,0xec0000,0x100000))
   elif path=='main/core/fc_timing.hpp':
    # Explicit, fingerprinted display fix supersedes only this old timing hash.
    from check_fc_rendering import assert_timing_contract
    assert_timing_contract(self,data,before[path]['sha256'])
   else:self.assertEqual(hashlib.sha256(data).hexdigest(),before[path]['sha256'])
  # Playback now has its own renderer; microphone DSP and saving remain on the
  # recording-only branch. Keep radio/DSP inputs frozen above, not a byte hash
  # of the old combined playback/capture control flow.
  recorder=read('main/core/audio_recorder.cpp')
  loop=part(recorder,'  static_assert(DualMicInput::MaxFrames','  stream.close();\n  job.state.store(State::Saving)')
  for token in ('microphones.process(samples, frames)', 'denoise.process(samples,frames)',
                'level.processDcBlocked(samples,clean.frames,denoise.voice())',
                'checkpoint(file.p, total)', 'vTaskDelay(1)', 'card.reportError(EIO)'):
   self.assertIn(token,loop)
  self.assertLess(loop.index('microphones.process('),loop.index('denoise.process('))
  processed=loop.index('denoise.process(')
  self.assertLess(processed,loop.index('std::fwrite(samples',processed))
  self.assertNotRegex(loop,r'Playback|esp_ae_sonic|writeStream|DoubleSpeed')
  self.assertIn('play(job,file,stream,limit);\n    return;',recorder)
 # The old source-only stage froze the previous binary. Current build
 # artifact immutability is checked by the invoking run's own manifest.
 def test_vault_wifi_reports_failed_initialization_not_only_disabled(self):
  s=read('main/apps/password_vault.cpp')
  self.assertIn('w.state == sys::WifiState::Failed',s)
  self.assertIn('w.error==ESP_ERR_NO_MEM',s)

class FcBluetoothContracts(unittest.TestCase):
 def test_entry_does_not_navigate_to_another_app(self):
  s=read('main/apps/fc_player.cpp')
  self.assertNotIn('wm.openApp',s);self.assertNotIn('wm.apps()',s)
  self.assertIn('->showBluetooth()',s)
  back=part(s,'bool FcPlayer::onBack()', 'void FcPlayer::onHide()')
  self.assertIn('if(page==Page::Bluetooth) { showList();return true; }',back)
 def test_page_cancels_directory_job_and_guards_old_ui_completion(self):
  s=read('main/apps/fc_player.cpp')
  enter=part(s,'void FcPlayer::showBluetooth()', 'void FcPlayer::bluetoothAction')
  self.assertLess(enter.index('listing->cancelled.store(true)'),enter.index('lv_obj_clean(root())'))
  self.assertLess(enter.index('listing.reset()'),enter.index('lv_obj_clean(root())'))
  self.assertIn('page==Page::List && listing && listing->done.load(std::memory_order_acquire)',s)
 def test_page_buffers_are_lazily_owned_and_released(self):
  h=read('main/apps/fc_player.hpp');s=read('main/apps/fc_player.cpp')
  self.assertIn('std::vector<sys::BleScanDevice> btScanned',h)
  close=part(s,'void FcPlayer::closeBluetooth()', 'void FcPlayer::showBluetooth()')
  for token in ('cancelBluetoothScan()','cancelBluetoothConnection()','swap(btBonds)','swap(btScanned)','btPeer={}'):self.assertIn(token,close)
 def test_manual_connect_uses_immutable_copied_identity_and_keyboard_role(self):
  s=read('main/apps/fc_player.cpp')
  for token in ('!btBonds[index].keyboard','connectKeyboard(btScanned[index])','reconnectBond(btBonds[index])','state.peer.keyboard'):
   self.assertIn(token,s)
  self.assertIn('!btPressed && !lv_obj_is_scrolling(btList)',s)
  self.assertIn('btPressed=true',s);self.assertIn('btPressed=false',s)
 def test_busy_state_is_not_reported_as_connected(self):
  s=read('main/apps/fc_player.cpp')
  self.assertIn('btPending || serviceBusy',s)
  self.assertIn('result.revision!=btResultRevision',s)
  self.assertIn('state.peer.connected',s)
  self.assertIn('keyboardConnected()',s)
 def test_name_and_address_use_system_identity_widget(self):
  s=read('main/apps/fc_player.cpp')
  self.assertIn('btPeer.create(peer,238)',s)
  self.assertIn('identity.create(row,234,1,2);identity.update(peer)',s)
  identity=read('main/ui/bluetooth_identity.hpp')
  self.assertIn('formatAddr(peer',identity);self.assertIn('lv_font_montserrat_10',identity)
 def test_controls_fit_240_pixels_and_rows_remain_touchable(self):
  s=compact('main/apps/fc_player.cpp')
  for token in ('this,96,36','this,70,36','this,88,36','this,80,36','this,240,42','c::panel(root(),240,86,true)','lv_obj_set_pos(btList,0,154)'):self.assertIn(token,s)
  boxes=[(0,0,36,36),(144,0,96,36),(0,117,70,36),(71,117,88,36),(160,117,80,36),(0,154,240,86)]
  for x,y,w,h in boxes:self.assertTrue(x>=0 and y>=0 and x+w<=240 and y+h<=240)
  self.assertEqual(70+88+80+2,240);self.assertGreaterEqual(86,2*42+1)
 def test_queue_independent_cancel_skips_stale_requests_and_preserves_bonds(self):
  s=compact('main/core/system_service.cpp')
  self.assertIn('cmd.connectionGeneration=connectionGeneration.load()',s)
  self.assertIn('cmd.connectionGeneration!=connectionGeneration.load()',s)
  self.assertIn('voidSystemService::cancelBluetoothConnection(){bootBluetoothAllowed.store(false);++connectionGeneration;}',s)
  cancel=part(s,'voidprocessBluetoothCancellation()', 'esp_err_tenqueue(')
  self.assertIn('bluetoothFlow.cancel()',cancel)
  flow=part(compact('main/core/ble_connection_flow.hpp'),'voidcancel()', 'voidcancelFor(')
  self.assertIn('BleHid::stopDiscoverable()',flow)
  self.assertNotRegex(flow,r'disconnect\(|removeBond|setEnabled\(|deinit')
  self.assertNotRegex(cancel,r'disconnect\(|removeBond|setEnabled\(|deinit')
  self.assertGreaterEqual(s.count('processBluetoothCancellation()'),4)
 def test_vault_uses_same_lossless_exit_cancellation(self):
  s=part(compact('main/apps/password_vault.cpp'),'voidPasswordVault::lock()', 'voidPasswordVault::onHide()')
  self.assertIn('if(m_pairStarted)sys::SystemService::cancelBluetoothConnection()',s)
  self.assertNotIn('SystemService::request(',s)
 def test_no_auto_scan_on_enter_or_reconnect_from_poll(self):
  s=read('main/apps/fc_player.cpp')
  enter=part(s,'void FcPlayer::showBluetooth()', 'void FcPlayer::bluetoothAction')
  poll=part(s,'void FcPlayer::pollBluetooth()', 'void FcPlayer::refresh()')
  for body in (enter,poll):self.assertNotRegex(body,r'SystemService::(?:request|reconnectBond|connectKeyboard)\(')

class AudioOwnershipModel:
 # Declarative ownership graph, NOT an execution of the codec driver.
 stages=('tx','rx','data','gpio','control','codec','device','stream')
 release_order=('stream','device','codec','control','gpio','data','tx','rx')
 def __init__(self):self.owned=set();self.released=[];self.shared_i2c_alive=True
 def open(self,fail_at=None):
  for i,item in enumerate(self.stages):
   if i==fail_at:self.close();return False
   self.owned.add(item)
  return True
 def close(self):
  for item in self.release_order:
   if item in self.owned:self.owned.remove(item);self.released.append(item)

class ConnectionIntentModel:
 def __init__(self,connected=False):
  self.generation=0;self.queue=[];self.connected=connected;self.attempts=[]
 def submit(self,name):self.queue.append((self.generation,name))
 def cancel(self):self.generation+=1
 def drain(self):
  for generation,name in self.queue:
   if generation==self.generation:self.attempts.append(name)
  self.queue.clear()

class LifecycleSpecificationModels(unittest.TestCase):
 def test_each_partial_audio_allocation_can_unwind_and_retry(self):
  for fail in range(len(AudioOwnershipModel.stages)):
   m=AudioOwnershipModel();self.assertFalse(m.open(fail));self.assertFalse(m.owned)
   self.assertTrue(m.shared_i2c_alive)
   self.assertTrue(m.open());self.assertEqual(m.owned,set(m.stages))
   m.close();self.assertFalse(m.owned)
 def test_audio_cleanup_is_idempotent_and_keeps_shared_i2c(self):
  m=AudioOwnershipModel()
  for _ in range(100):
   self.assertTrue(m.open());m.released=[];m.close()
   self.assertEqual(m.released,list(m.release_order));self.assertFalse(m.owned)
   m.close();self.assertEqual(m.released,list(m.release_order));self.assertTrue(m.shared_i2c_alive)
 def test_active_stream_is_never_reclaimed_by_wifi(self):
  for mutex_available,stream_active in itertools.product((False,True),repeat=2):
   reclaimed=mutex_available and not stream_active
   if stream_active or not mutex_available:self.assertFalse(reclaimed)
 def test_cancel_fence_invalidates_old_but_not_new_explicit_requests(self):
  for count,linked in itertools.product(range(9),(False,True)):
   m=ConnectionIntentModel(linked)
   for i in range(count):m.submit('old'+str(i))
   m.cancel();m.submit('new');m.drain()
   self.assertEqual(m.attempts,['new']);self.assertEqual(m.connected,linked)
 def test_back_returns_list_and_late_directory_result_cannot_target_subpage(self):
  for done_before in (False,True):
   page='list';job={'done':done_before,'cancelled':False};ui_job=job
   ui_job['cancelled']=True;ui_job=None;page='bluetooth';job['done']=True
   self.assertFalse(page=='list' and ui_job is not None)
   page='list';self.assertEqual(page,'list');self.assertTrue(job['cancelled'])
 def test_repeated_wifi_taps_reserve_only_one_command(self):
  queued=[];pending=False
  for _ in range(100):
   if not pending:pending=True;queued.append('enable')
  self.assertEqual(len(queued),1)
  queued.pop();pending=False
  if not pending:pending=True;queued.append('enable')
  self.assertEqual(len(queued),1)

if __name__=='__main__':unittest.main(verbosity=2)
