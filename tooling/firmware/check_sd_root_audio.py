"""Current root-SD and recorder-level source checks (no hardware assumptions)."""
from pathlib import Path
import hashlib,json,re,tempfile,unittest,zipfile
from package_sd_resources import write_template_archive
from urllib.parse import urlsplit
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/sd-root-audio-20260919-0020'
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(text):return re.sub(r'\s+','',text)
class SdRootAudioContracts(unittest.TestCase):
 def test_production_change_scope_is_exact(self):
  before=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
  allowed={'main/main.cpp','main/apps/settings_system.cpp','main/core/sd_media.hpp','main/core/sd_media.cpp','main/core/vault_connection.hpp','main/core/vault_connection.cpp','main/core/fc_emulator.cpp','main/apps/fc_player.cpp','main/core/audio_mgr.cpp','main/core/audio_mgr.hpp','main/core/audio_recorder.cpp'}
  changed=set()
  for name,item in before.items():
   if not name.startswith(('main/','components/')) and name not in ('sdkconfig','sdkconfig.defaults','partitions.csv','dependencies.lock','CMakeLists.txt'):continue
   self.assertTrue((ROOT/name).is_file(),name)
   if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=item['sha256']:changed.add(name)
  self.assertEqual(changed,allowed)
 def test_no_old_sd_root_in_any_production_source(self):
  for p in (ROOT/'main').rglob('*'):
   if p.is_file() and p.suffix in ('.hpp','.h','.cpp','.c'):
    data=p.read_bytes()
    self.assertNotIn(b'/sdcard/joycode',data,str(p))
    self.assertNotIn(b'joycode/nes',data,str(p))
    self.assertNotIn(b'joycode/config',data,str(p))
 def test_all_three_apps_use_root_directories(self):
  self.assertIn('"/sdcard/config/vault.json"',read('main/core/vault_connection.hpp'))
  self.assertIn('"/sdcard/config/nes-keys.json"',read('main/core/fc_emulator.cpp'))
  self.assertIn('"/sdcard/recordings"',read('main/core/sd_media.hpp'))
  self.assertIn('"/sdcard/nes"',read('main/core/sd_media.hpp'))
  self.assertIn('SD卡 /nes',read('main/apps/fc_player.cpp'))
 def test_mountpoint_is_checked_not_created(self):
  s=compact(read('main/core/sd_media.cpp'))
  self.assertIn('stat(Root,&st)==0&&S_ISDIR(st.st_mode)&&directory(path)',s)
  self.assertNotIn('directory(Root)',s)
  self.assertIn('!managedDirectory(path)',s)
  self.assertIn('std::strcmp(path, Patterns)',read('main/core/sd_media.hpp'))
 def test_template_moved_without_changing_configuration_contents(self):
  before=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
  sd=ROOT/'../resources/sdcard'
  self.assertFalse((sd/'joycode').exists())
  # The distributable template uses a replaceable host, not the author's LAN IP.
  vault=json.loads((sd/'config/vault.json').read_text(encoding='utf-8'))
  self.assertEqual(set(vault),{'schema','url'})
  self.assertEqual(vault['schema'],1)
  url=urlsplit(vault['url'])
  self.assertIn(url.scheme,('http','https'))
  self.assertTrue(url.hostname)
  self.assertIsNone(url.username)
  self.assertIsNone(url.password)
  expected=before['sdcard/joycode/config/nes-keys.json']
  self.assertEqual(hashlib.sha256((sd/'config/nes-keys.json').read_bytes()).hexdigest(),expected['sha256'])
  for name in ('config','recordings','nes'):self.assertTrue((sd/name).is_dir(),name)
  with tempfile.TemporaryDirectory(prefix='yumo-sd-template-') as temporary:
   archive=Path(temporary)/'sd-template.zip'
   write_template_archive(sd,archive)
   with zipfile.ZipFile(archive) as z:
    self.assertIsNone(z.testzip())
    self.assertEqual(sorted(z.namelist()),sorted(('config/','recordings/','nes/','config/vault.json','config/nes-keys.json')))
    for name in ('vault.json','nes-keys.json'):self.assertEqual(z.read('config/'+name),(sd/'config'/name).read_bytes())
 def test_hardware_gain_has_readback_and_failure_cleanup(self):
  s=compact(read('main/core/audio_mgr.cpp'))
  self.assertIn('ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)|ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)',s)
  self.assertIn('format.channel_mask=record?micChannels:0',s)
  self.assertIn('format.channel=record?2:1',s)
  self.assertIn('s_stream,micChannels,36.0f',s)
  self.assertIn('micGainFirstRegister=0x43,micGainCode=13',s)
  self.assertIn('for(intchannel=0;channel<2;++channel)',s)
  self.assertIn('esp_codec_dev_read_reg(s_stream,micGainFirstRegister+channel,&gainRegister)==ESP_CODEC_DEV_OK',s)
  self.assertIn('(gainRegister&0x0f)==micGainCode',s)
  self.assertIn('releaseAudioLocked();returnfalse',s)
  cleanup=s.split('voidreleaseAudioLocked()',1)[1].split('}//namespace',1)[0]
  self.assertIn('s_stream=nullptr;s_recording=false',cleanup)
  self.assertIn('bsp_audio_deinit()',cleanup)
  self.assertEqual(s.count('esp_codec_dev_read_reg('),1)
 def test_gain_encoding_matches_installed_es7210_driver(self):
  header=read('../dependencies/managed_components/espressif__esp_codec_dev/device/es7210/es7210_reg.h')
  body=header.split('typedef enum gain_value {',1)[1].split('}',1)[0]
  names=re.findall(r'GAIN_[A-Z0-9_]+',body)
  self.assertEqual(names.index('GAIN_36DB'),13)
  self.assertIn('#define ES7210_MIC1_GAIN_REG43       0x43',header)
  self.assertIn('#define ES7210_MIC2_GAIN_REG44       0x44',header)
 def test_processing_is_before_sd_write_and_record_only(self):
  s=compact(read('main/core/audio_recorder.cpp'))
  self.assertEqual(s.count('level.processDcBlocked('),2)
  section=s.split('if(!sys::Audio::readStream',1)[1].split('unsignedpeak=0;',1)[0]
  self.assertLess(section.index('microphones.process('),section.index('level.processDcBlocked('))
  self.assertLess(section.index('level.processDcBlocked('),section.index('std::fwrite('))
  self.assertIn('microphones.reset();level.reset();',s)
  self.assertIn('play(job,file,stream,limit);return;',s)
  self.assertIn('if(!wasPaused)',s)
  self.assertIn('if(!flushTail())break;',s)
  self.assertIn('int16_tsamples[DualMicInput::MaxFrames*DualMicInput::Channels]',s)
 def test_capture_bytes_and_wav_duration_use_different_channel_counts(self):
  s=compact(read('main/core/audio_recorder.cpp'))
  for token in ('MonoBlockBytes=RecordingLevel::MaxSamples*sizeof(int16_t)',
                'std::min<uint32_t>(MonoBlockBytes,limit-total)',
                'readStream(samples,size*DualMicInput::Channels)',
                'frames=size/sizeof(samples[0])','microphones.process(samples,frames)',
                'std::fwrite(samples,1,outputBytes,file.p)','total+=outputBytes','job.bytes.store(total)',
                'writeStream(samples,block.frames*sizeof(int16_t))'):
   self.assertIn(token,s)
  self.assertNotIn('std::min<uint32_t>(sizeof(samples)',s)
  self.assertIn('interleaved MIC1/MIC2',read('main/core/audio_mgr.hpp'))
 def test_dual_mic_is_small_and_has_hysteresis_no_allocation(self):
  s=compact(read('main/core/dual_mic_input.hpp'))
  for token in ('Channels=2','MaxFrames=512','FadeFrames=64','HoldBlocks=3',
                'dcQ8_[Channels]','candidate.mean>current.mean*2',
                'current.severe(frames)','candidate.clipped*4<current.clipped',
                '++pending_>=HoldBlocks','frames>MaxFrames','sizeof(DualMicInput)<=16'):
   self.assertIn(token,s)
  self.assertNotRegex(s,r'\b(?:malloc|calloc|new|fopen|vTaskDelay|ESP_LOG|lv_)\b')
 def test_dual_mic_model_uses_production_headers(self):
  s=read('../tooling/firmware/dual_mic_input_model_test.cpp')
  for name in ('dual_mic_input.hpp','recording_level.hpp','wav_format.hpp'):
   self.assertIn('#include "core/'+name+'"',s)
  for check in ('eitherMicCanRecord','oppositePhaseDoesNotCancel','transientDoesNotSwitch',
                'sustainedStrongerSwitches','clippingChoosesUsableOtherMic',
                'independentDcAndSilence','resetForPauseMatchesFresh',
                'invalidAndShortFramesBounded','combinedGainAndMonoTiming',
                'dcBlockedPathDoesNotFilterTwice'):
   self.assertIn('static_assert('+check+'())',s)
 def test_processor_is_small_bounded_and_has_no_side_effects(self):
  s=compact(read('main/core/recording_level.hpp'))
  for token in ('MaxSamples=512','BaseGain=2*One','MaxGain=8*One','Ceiling=30000','count>MaxSamples','absoluteSum/count>=96','dcQ8_','headroom','sizeof(RecordingLevel)<=16'):
   self.assertIn(token,s)
  self.assertNotRegex(s,r'\b(?:malloc|calloc|new|fopen|vTaskDelay|ESP_LOG|lv_)\b')
 def test_no_new_background_job_or_playback_gain_change(self):
  s=compact(read('main/core/audio_mgr.cpp'))
  self.assertIn('else(void)esp_codec_dev_set_out_vol(s_stream,Settings::volume())',s)
  self.assertNotIn('Settings::setVolume(100)',s)
  self.assertNotIn('RecordingLevel',read('main/core/fc_emulator.cpp'))
  self.assertNotIn('RecordingLevel',read('main/main.cpp'))
 def test_model_exercises_actual_processor_not_python_replica(self):
  s=read('../tooling/firmware/recording_level_model_test.cpp')
  self.assertIn('#include "core/recording_level.hpp"',s)
  for check in ('silenceAndInvalidBuffers','lowSpeechBoost','loudStepAndNoWrap','quietNoiseDoesNotPump','dcBiasRemoved','resetForResumeAndNewFile','sustainedLoudStaysBounded'):
   self.assertIn('static_assert('+check+'())',s)
 def test_build_marker_matches_about_screen(self):
  self.assertIn('YUMO-INPUT-KEYS-20260921',read('main/main.cpp'))
  s=read('main/apps/settings_system.cpp')
  for field in ('esp_app_get_description()', 'desc->version', 'desc->date', 'desc->time', 'desc->app_elf_sha256'):self.assertIn(field,s)
  self.assertNotIn('SD-MEDIA-FC-20260918',s)
if __name__=='__main__':unittest.main(verbosity=2)
