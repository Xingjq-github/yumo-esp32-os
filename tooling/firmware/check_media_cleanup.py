"""Ownership and delivery contracts for media cleanup and USB disk mode."""
from pathlib import Path
import re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8')
class MediaOwnership(unittest.TestCase):
    def test_silent_game_audio_allows_sleep_but_teardown_waits(self):
        self.assertIn('core::game_audio::busy()',read('main/core/power_runtime.cpp'))
        self.assertIn('!core::game_audio::idle()',read('main/core/power_mgr.cpp'))
        s=read('main/core/game_audio.cpp')
        self.assertIn('sys::Audio::closeStream();opened=false;',s)
        self.assertIn('audible.store(false);active.store(false)',s)
    def test_assistant_removed_from_firmware(self):
        forbidden=r'voice_assistant|VoiceAssistant|personal_wake|voice_control|esp-sr|gmf_ai_audio'
        for base in ('main','components'):
            for path in (ROOT/base).rglob('*'):
                if path.is_file() and path.suffix in ('.cpp','.hpp','.h','.c','.txt','.yml'):
                    self.assertNotRegex(path.read_text(encoding='utf-8',errors='replace'),forbidden,str(path))
        self.assertNotIn('model,',read('partitions.csv'))
        self.assertNotRegex(read('main/CMakeLists.txt'),forbidden)
    def test_vault_uses_bounded_internal_io_before_any_erase(self):
        s=read('main/core/vault_storage.cpp')
        self.assertIn('Chunk = 1024',s)
        self.assertIn('MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT',s)
        self.assertIn('wipe(buffer, Chunk); heap_caps_free(buffer)',s)
        self.assertEqual(s.count('esp_partition_read('),1)
        self.assertEqual(s.count('esp_partition_write('),1)
        commit=s.split('bool commit(',1)[1]
        self.assertLess(commit.index('if(!io)'),commit.index('esp_partition_erase_range('))
        self.assertLess(commit.index('io.read(p, offset + Sector'),commit.index('io.write(p, offset, h.data()'))
    def test_recording_delete_waits_for_closed_file(self):
        s=read('main/apps/recorder.cpp')
        self.assertIn('files[index].path!=playPath',s)
        self.assertIn('pendingDeletePath=self->deletePath',s)
        self.assertIn('pendingDeleteGeneration=self->filesGeneration',s)
        self.assertIn('if(!pendingDeletePath.empty()&&!job&&core::recorder::idle())',s)
        self.assertIn('deleteRecording(pendingDeletePath,pendingDeleteGeneration,pendingDeleteIdentity)',s)
        cancel=s.split('auto cancel=c::button(dialog,',1)[1].split('auto confirm=',1)[0]
        self.assertIn('closeDelete()',cancel);self.assertNotIn('stop.store',cancel)
    def test_usb_only_releases_filesystem_after_raw_teardown(self):
        s=read('main/core/usb_storage.cpp')
        self.assertLess(s.index('tinyusb_driver_uninstall()'),s.index('sdmmc_host_deinit()'))
        self.assertLess(s.index('sdmmc_host_deinit()'),s.index('releaseUsb(clean)'))
        self.assertNotIn('hostSeen',s)
        stop=s.split('esp_err_t UsbStorage::setEnabled(bool on)',1)[1].split('if(Power::sleepPending()',1)[0]
        self.assertIn('stop.store(true);return ESP_OK;',stop)
        self.assertNotIn('ESP_ERR_NOT_FINISHED',stop)
        teardown=s.split('mode.store(Mode::Stopping);admit.store(false);',1)[1]
        self.assertLess(teardown.index('lock(ioMutex)'),teardown.index('flushCard()'))
        self.assertLess(teardown.index('flushCard()'),teardown.index('tud_disconnect()'))
        self.assertIn('if(clean&&mediaClean&&core::sd::state()==core::sd::State::Ejected)',s)
        self.assertIn('cancelUsbRequest()',s)
        self.assertIn('void worker(void *) {\n    execute();',s)
        sd=read('main/core/sd_media.cpp')
        self.assertIn('usbFence.store(!clean)',sd)
        self.assertIn('(usbFence.load()&&!finishing)||current==State::Usb',sd)
    def test_usb_switch_is_confirmed_on_device_and_volatile_at_boot(self):
        ui=read('main/apps/settings_system.cpp');dialogs=read('main/apps/settings.cpp')
        self.assertIn('self->ask(Dialog::UsbFlash,',ui)
        self.assertIn('未完成的传输可能丢失',ui)
        self.assertIn('case Dialog::UsbFlash: err=sys::UsbStorage::setEnabled(false)',dialogs)
        s=read('main/core/usb_storage.cpp')
        self.assertIn('mode{Mode::Off}',s);self.assertIn('running{false}',s)
        self.assertNotIn('nvs_',s)
    def test_maze_audio_pauses_and_releases_without_ui_owned_worker(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertIn('start(core::game_audio::Track::Maze)',s)
        self.assertIn('if(audio)audio->pause()',s)
        for effect in ('Impact','Fall','Goal'):self.assertIn('core::game_audio::'+effect,s)
        self.assertIn('uint32_t(now-lastImpact)>=160',s)
        self.assertIn('audio->requestStop();audio.reset()',s)
        worker=read('main/core/game_audio.cpp')
        self.assertIn('if(!sys::Power::audioMayRun())job->pause(false)',worker)
        self.assertNotIn('GravityMaze',worker)
    def test_usb_callbacks_are_synchronous_and_fenced(self):
        s=read('main/core/usb_storage.cpp')
        self.assertIn('const std::lock_guard<std::mutex> lock(ioMutex)',s)
        self.assertIn('MMC_R1_READY_FOR_DATA',s)
        self.assertIn('MMC_R1_CURRENT_STATE_TRAN',s)
        self.assertIn('void msc_storage_mount_to_usb() {}',s)
        self.assertIn('void msc_storage_mount_to_app() {}',s)
        self.assertNotIn('__wrap_tud_',s)
        self.assertNotIn('tinyusb_msc_new_storage',s)
        self.assertIn('return sdmmc_write_sectors(&card,data,lba,1)',s)
if __name__=='__main__':unittest.main(verbosity=2)
