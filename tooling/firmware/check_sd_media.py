"""SD/media/FC source-contract checks. These do not replace device acceptance."""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/sd-media-fc-20260918-221715'
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(text):return re.sub(r'\s+','',text)
class SdMediaContracts(unittest.TestCase):
 def test_exact_scope_preserves_bluetooth_wifi_battery_and_pin_models(self):
  allowed={'main/CMakeLists.txt','main/main.cpp','main/apps/settings_system.cpp','main/apps/password_vault.cpp','main/core/audio_mgr.cpp','main/core/audio_mgr.hpp','main/core/password_vault.cpp','main/core/password_vault.hpp','main/core/power_mgr.cpp','main/core/storage_browser.cpp','main/core/storage_browser.hpp','main/ui/external_keyboard.cpp','main/ui/external_keyboard.hpp','main/ui/lv_font_menu_14.c'}
  for path,sha in json.loads((RUN/'before.json').read_text()).items():
   path=path.replace('\\','/')
   if not path.startswith(('main/','components/')) or path in allowed:continue
   self.assertTrue((ROOT/path).is_file(),path)
   self.assertEqual(hashlib.sha256((ROOT/path).read_bytes()).hexdigest(),sha,path)
 def test_cmake_existing_sources_and_added_apps_are_unique(self):
  main_sources=read('main/CMakeLists.txt').split('set(MAIN_SRCS',1)[1].split('\n)',1)[0]
  sources=re.findall(r'"([^"]+\.(?:c|cpp))"',main_sources)
  self.assertEqual(len(sources),len(set(sources)))
  self.assertEqual(sources.count('apps/settings_storage.cpp'),1)
  self.assertEqual(sources.count('ui/quick_panel.cpp'),1)
  self.assertEqual(sources.count('ui/lv_font_reading_15.c'),1)
  for path in sources:self.assertTrue((ROOT/'main'/path).is_file(),path)
  names=re.findall(r'registerApp\(\s*"([^"]+)"',read('main/main.cpp'))
  self.assertEqual(names,['密码本','时钟','计时器','文本','蓝牙键鼠','相册','录音机','随机','万年历','编程速查','设计模式','蓝牙音乐','MP3音乐','8Bit音乐','手电筒','摩斯电码','文件管理','萌芽物语','局域网','网速测速','FC游戏','小恐龙','重力迷宫','小智AI','设置'])
  self.assertIn('YUMO-INPUT-KEYS-20260921',read('main/main.cpp'))
  self.assertIn('esp_app_get_description()',read('main/apps/settings_system.cpp'))
  for worker in ('recorder::start','fc::start'):self.assertNotIn(worker,read('main/main.cpp'))
 def test_examples_have_no_credentials(self):
  doc=json.loads(read('../resources/sdcard/config/vault.json'))
  self.assertEqual(set(doc),{'schema','url'});self.assertEqual(doc['schema'],1)
  self.assertRegex(doc['url'],r'^http://[^/]+$')
  keys=json.loads(read('../resources/sdcard/config/nes-keys.json'))
  self.assertEqual(keys['schema'],1);self.assertEqual(len(keys['keys']),10)
  self.assertEqual(len(set(keys['keys'].values())),10)
 def test_connection_parser_bounded_and_only_transport_fields(self):
  s=compact(read('main/core/vault_connection.cpp'))
  for token in ('json.size()>1024','fields!=2','normalizeOrigin(url->valuestring,origin)','out.url=origin','version->valuedouble!=1','char data[1025]'.replace(' ',''),'std::fclose(file)'):
   self.assertIn(token,s)
  for token in ('configEncode','commit(','Pin','Token','Key'):
   self.assertNotIn(token,read('main/core/vault_connection.cpp'))
 def test_exactly_one_primary_attempt_and_one_sd_attempt(self):
  s=compact(read('main/core/password_vault.cpp'))
  self.assertEqual(s.count('attempt(ConnectionConfig{current.url})'),1)
  self.assertEqual(s.count('attempt(fromSd)'),1)
  self.assertEqual(s.count('ConnectionConfig::readSd('),1)
  a=s.index('if(!attempt(ConnectionConfig{current.url}))');b=s.index('ConnectionConfig::readSd');c=s.index('if(!attempt(fromSd))')
  self.assertLess(a,b);self.assertLess(b,c)
  self.assertIn('if(!attempt(fromSd)){job.error="同步失败："+job.error;return;}',s)
 def test_sd_endpoint_only_persisted_after_bundle_validation(self):
  s=compact(read('main/core/password_vault.cpp'))
  a=s.index('autoattempt=');b=s.index('if(!attempt(ConnectionConfig{current.url}))')
  for check in ('framing(','current.id','current.salt','decode(job,bundle,current.key)'):
   self.assertIn(check,s[a:b])
  self.assertLess(s.index('if(!attempt(fromSd))'),s.index('job.sdConfigUsed=true'))
  self.assertLess(s.index('job.sdConfigUsed=true'),s.index('configEncode(*result,config)'))
  self.assertNotIn('current.revision',s[a:b])
  self.assertIn('bundle==disk.bundle&&result->url==current.url',s)
 def test_visible_fallback_and_cooperative_cancellation(self):
  s=compact(read('main/core/password_vault.cpp'))
  self.assertIn('job.syncStage.store(1)',s);self.assertIn('job.syncStage.store(2)',s)
  self.assertIn('i<10&&!stopped(job)',s)
  ui=read('main/apps/password_vault.cpp')
  self.assertIn('syncStage.load()',ui);self.assertIn('SD',ui)
 def test_private_typing_guards_unchanged_despite_sync_progress_delay(self):
  # Old contract banned a 100ms wait anywhere in this entire file. New SD wait
  # is unrelated to the sensitive HID send loop, which is checked explicitly.
  s=compact(read('main/core/password_vault.cpp'))
  for token in ('printable(text.data(),text.size())','privateLinkGeneration()!=job.link','if(i+1<text.size())vTaskDelay(pdMS_TO_TICKS(interaction::KeyGapMs))'):
   self.assertIn(token,s)
  ble=compact(read('main/core/ble_hid.cpp'))
  for token in ('s.link==s_linkGeneration.load()','s.permit==s_privatePermit.load()','if(s.releasing)'):self.assertIn(token,ble)
 def test_sd_only_known_directories_and_no_formatting(self):
  s=compact(read('main/core/sd_media.cpp'))
  self.assertIn('!managedDirectory(path)',s)
  self.assertIn('errno=EINVAL',s)
  self.assertIn('path.find',s);self.assertIn('name.find_first_of',s)
  for path in ('main/core/sd_media.cpp','main/core/audio_recorder.cpp','main/core/fc_emulator.cpp'):
   self.assertNotRegex(read(path),r'(?:format_if_mount_failed|esp_vfs_fat_.*format|unlink\(|remove\()')
 def test_recorder_unique_time_name_and_temporary_file(self):
  s=read('main/core/audio_recorder.cpp')
  for token in ('%Y-%m-%d_%H-%M-%S','%03ld_%02u.wav','.part','O_EXCL','now.tm_year + 1900 < 2024'):
   self.assertIn(token,s)
  self.assertLess(s.index('if (!stream.open(true))'),s.index('if (!create(job, file, temporary))'))
 def test_recording_finalization_and_periodic_sync(self):
  s=compact(read('main/core/audio_recorder.cpp'))
  for token in ('fsync(fileno(file))','wav::header(bytes)','total-flushed>=wav::ByteRate','std::rename(temporary.c_str(),job.path.c_str())','constintclosed=std::fclose(file.p)'):
   self.assertIn(token,s)
  self.assertLess(s.index('constboolsaved=checkpoint(file.p,total)'),s.index('std::rename('))
  self.assertIn('if(!saved||closed!=0)',s)
 def test_pause_retains_file_but_closes_audio_and_releases_performance_lock(self):
  s=compact(read('main/core/audio_recorder.cpp'))
  paused=s.split('if(job.pause.load()&&direction==0){',1)[1].split('sys::Power::WorkGuardwork;',1)[0]
  self.assertIn('stream.close()',paused);self.assertIn('continue;',paused)
  self.assertNotIn('std::fclose',paused);self.assertNotIn('create(job',paused)
  self.assertIn('player->seek(total)',paused)
 def test_playback_is_bounded_supported_wav_only(self):
  s=compact(read('main/core/audio_recorder.cpp'))
  for token in ('sd::child(job.path,sd::Recordings,".wav")','wav::parse(h,st.st_size,limit)','limit-total','std::fread(samples,1,bytes,file.p)!=bytes'):
   self.assertIn(token,s)
  player=compact(read('main/core/recorder_playback.hpp'))
  self.assertIn('std::min<uint32_t>(InputFrames*2,length_-feed_)',player)
  self.assertIn('rewindBlockBytes(position_)',player)
 def test_audio_is_serialized_and_microphone_is_channel_selected(self):
  s=compact(read('main/core/audio_mgr.cpp'))
  for method in ('init','applyVolume','suspendForStandby','resumeFromStandby','openStream','readStream','writeStream','closeStream','releaseIdle'):
   body=s.split('Audio::'+method+'(',1)[1].split('\n}',1)[0]
   self.assertIn('codecMutex',body)
  for token in ('if(s_stream||rate<8000||rate>48000','format.channel=record?2:1','ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)','esp_codec_dev_close(s_stream)'):
   self.assertIn(token,s)
  self.assertIn('!core::fc::idle()',read('main/core/audio_recorder.cpp'))
  self.assertIn('!core::recorder::idle()',read('main/core/fc_emulator.cpp'))
 def test_recorder_compact_controls_and_exit_stop(self):
  s=compact(read('main/apps/recorder.cpp'))
  for token in ('this,layout::Record.w,layout::Record.h','this,121,40','layout::RowHeight','layout::DeleteWidth','job->requestStop()','lv_timer_delete(timer)'):
   self.assertIn(token,s)
  self.assertIn('State::Paused',s);self.assertIn('LV_SYMBOL_PLAY',s)
 def test_active_apps_keep_awake_without_overriding_pwr(self):
  for path in ('main/apps/recorder.cpp','main/apps/fc_player.cpp'):
   s=read(path);self.assertIn('lv_display_trigger_activity(nullptr)',s);self.assertIn('sys::Power::isScreenOn()',s)
   self.assertNotIn('Power::screenOn(',s)
  for path in ('main/core/audio_recorder.cpp','main/core/fc_emulator.cpp'):
   self.assertIn('!sys::Power::audioMayRun()' if 'audio_recorder' in path else '!sys::Power::isScreenOn()',read(path));self.assertIn('sys::Power::sleepPending()',read(path))
 def test_deep_sleep_waits_for_both_media_workers_before_unmount(self):
  s=compact(read('main/core/power_mgr.cpp'))
  self.assertIn('if(!core::xiaozhi::idle()||!core::mp3::idle()||!core::chip::idle()||!core::game_audio::idle()||!core::storageIdle()||!vault::idle()||!core::recorder::idle()||!core::fc::idle())',s)
  self.assertLess(s.index('if(!core::xiaozhi::idle()||!core::mp3::idle()||!core::chip::idle()||!core::game_audio::idle()||!core::storageIdle()'),s.index('bsp_sdcard_unmount()'))
  self.assertIn('>10000000',s);self.assertIn('appstorageworkerbusy',s)
 def test_worker_lifetime_and_psram_allocations(self):
  for path in ('main/core/audio_recorder.cpp','main/core/fc_emulator.cpp'):
   s=compact(read(path));self.assertIn('std::shared_ptr<Job>',s)
   self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',s)
   self.assertIn('vTaskDeleteWithCaps(nullptr)',s)
   self.assertNotIn('lv_',s);self.assertNotIn('ui::',s)
   self.assertLess(s.index('active.store(false);\nvTaskDeleteWithCaps'.replace('\n','')),s.index('std::shared_ptr<Job>start'))
 def test_fc_mapper_whitelist_matches_compiled_registry(self):
  s=read('components/nofrendo/mappers/mappers.h')
  found=set(map(int,re.findall(r'&map(\d+)_intf',s)))
  parser=read('components/nofrendo/nes/ines.h').split('INES_INLINE size_t ines_ram',1)[0]
  self.assertEqual(found,set(map(int,re.findall(r'case (\d+):',parser))))
  self.assertEqual(len(found),35)
  cmake=read('components/nofrendo/CMakeLists.txt')
  self.assertNotIn('GLOB',cmake);self.assertNotIn('nes/state.c',cmake)
  self.assertTrue((ROOT/'components/nofrendo/COPYING').is_file())
 def test_fc_rom_screen_and_frame_bounds(self):
  s=compact(read('main/core/fc_emulator.cpp'))
  for token in ('sd::child(job.path,sd::Games,".nes")','ines_parse(header,size_t(st.st_size),&romInfo)','std::min<size_t>(4096','NES_SCREEN_OVERDRAW','engine.audioTried'):
   self.assertIn(token,s)
  self.assertIn('Width=240,Height=240',compact(read('main/core/fc_video.hpp')))
  self.assertIn('std::try_to_lock',read('main/apps/fc_player.cpp'))
 def test_fc_config_validated_then_applied(self):
  s=compact(read('main/core/fc_emulator.cpp'))
  for token in ('json.size()>2048','cJSON_GetArraySize(doc.value)!=2','seen[i]','result.usage[i]==result.usage[j]','mapping=result','job.mapping=Mapping{}'):
   self.assertIn(token,s)
  self.assertLess(s.index('loadMapping(job,card)'),s.index('card.release()'))
  self.assertLess(s.index('card.release()'),s.index('job.ready.store(true'))
 def test_fc_input_capture_does_not_navigate_lvgl(self):
  s=compact(read('main/ui/external_keyboard.cpp'))
  self.assertIn('raw=rawHandler&&!controller&&!QuickPanel::visible()&&!BluetoothPairing::root()',s)
  self.assertIn('controller=sys::BleKeyboardClient::iineKeyboard()',s)
  self.assertIn('if(controller){if(applyController(keys))break;}',s)
  self.assertIn('if(raw)rawHandler(keys,rawContext);elseapply(keys)',s)
  self.assertIn('if(!raw&&repeatUsage',s)
  self.assertIn('if(rawHandler)rawHandler({},rawContext)',s)
  ui=compact(read('main/apps/fc_player.cpp'))
  self.assertIn('ui::ExternalKeyboard::capture(keys,this)',ui)
  self.assertIn('ui::ExternalKeyboard::capture(nullptr,nullptr)',ui)
  self.assertIn('sys::BleHid::keyboardConnected()',ui)
 def test_model_file_uses_real_headers(self):
  s=read('../tooling/firmware/sd_media_model_test.cpp')
  self.assertIn('#include "core/fc_model.hpp"',s);self.assertIn('#include "core/wav_format.hpp"',s)
  self.assertIn('static_assert(romModel())',s);self.assertIn('static_assert(wavModel())',s)
if __name__=='__main__':unittest.main(verbosity=2)
