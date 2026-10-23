"""SD hot-plug source/lifetime contracts; no compilation or hardware access.

These guards cannot establish electrical hot-swap safety or simulate FatFs.
Removal/reinsertion during idle/read/write still needs a flashed-device test.
"""
from pathlib import Path
import re
import unittest

ROOT = (Path(__file__).resolve().parents[2]/'firmware')


def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


def part(source, begin, end):
    return source.split(begin, 1)[1].split(end, 1)[0]


class SdHotplugContracts(unittest.TestCase):
    def test_recovery_is_bounded_and_only_when_no_handles_are_open(self):
        source = read('main/core/sd_media.cpp')
        begin = part(source, 'Session::Session(Access access,bool recover,bool drain)', 'Session::~Session()')
        active = part(begin, 'if (users) {', '} else if (recover) {')
        self.assertNotRegex(active, r'sdmmc_get_status|bsp_sdcard_(?:un)?mount\(')
        self.assertIn('damaged || !card', active)
        self.assertIn('error_ = EBUSY; return;', active)
        self.assertLess(begin.index('sdmmc_get_status(card)'), begin.index('bsp_sdcard_unmount()'))
        self.assertLess(begin.index('bsp_sdcard_unmount()'), begin.index('bsp_sdcard_mount()'))
        self.assertEqual(begin.count('bsp_sdcard_mount()'), 1)
        self.assertNotRegex(begin, r'\b(?:while|for)\s*\(')
        self.assertIn('error_=EIO;teardownFailed=true;failure.store(Failure::Unmount)', begin)
        self.assertIn('if (!card) {', begin)  # Also handles boot without a card.
        self.assertLess(begin.index('error_ = result == ESP_ERR_NO_MEM'), begin.index('++users'))

    def test_no_formatting_polling_or_changed_cd_pin_assumption(self):
        bsp = read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        mount = part(bsp, 'esp_err_t bsp_sdcard_sdmmc_mount(', 'esp_err_t bsp_sdcard_sdspi_mount(')
        self.assertIn('.format_if_mount_failed = false', mount)
        self.assertNotIn('.format_if_mount_failed = true', mount)
        self.assertIn('config->cd = SDMMC_SLOT_NO_CD', bsp)
        source = read('main/core/sd_media.cpp')
        self.assertNotRegex(source, r'xTaskCreate|lv_timer|esp_timer_create|vTaskDelay|ESP_LOG')

    def test_gui_does_not_wait_and_worker_owns_streaming_sd_lifetime(self):
        source = read('main/core/sd_media.cpp')
        self.assertIn('else if (!lock.try_lock()) { error_ = EBUSY; return; }', source)
        gui=read('main/apps/photo_viewer.cpp')
        self.assertNotRegex(gui,r'\bfopen\(|jpeg_dec_|photo_png_read|sd::Session')
        worker=read('main/core/photo_loader.cpp')
        owner = part(worker, 'struct Input {', 'bool readSource')
        self.assertLess(owner.index('card(c.job.sdAccess)'), owner.index('std::fopen('))
        close = part(owner, 'bool close()', 'bool failure(')
        self.assertLess(close.index('std::fclose(file)'), close.index('card.release()'))
        self.assertIn('~Input() {close();}', owner)
        self.assertIn('card.reportError(errno?errno:EIO)', owner)
        self.assertIn('job.source->generation!=card.generation()', owner)
        load = part(worker, 'bool readSource', 'int readPng')
        self.assertIn('s.length<=CacheBytes', load)
        self.assertLess(load.index('!input->close()'), load.index('input.reset()'))
        refill = part(owner, 'bool read(', 'static int callback')
        self.assertLess(refill.index('cancelled(&context)'), refill.index('std::fread('))
        self.assertIn('if(sd::busy())', worker)
        self.assertNotRegex(owner + load, r'bsp_sdcard_|sdmmc_get_status')

    def test_leases_are_not_copyable_and_destructors_only_release(self):
        header = read('main/core/sd_media.hpp')
        self.assertIn('Session(const Session &) = delete', header)
        self.assertIn('Session &operator=(const Session &) = delete', header)
        source = read('main/core/sd_media.cpp')
        self.assertIn('Session::~Session() { release(); }', source)
        release = part(source, 'void Session::release()', 'void Session::reportError(')
        self.assertIn('if (!held_) return;', release)
        self.assertIn('--users', release)
        self.assertNotRegex(release, r'bsp_sdcard_|sdmmc_|fclose|closedir')
        fault = part(source, 'void Session::reportError(', 'bool ensure(')
        for error in ('EIO', 'ENODEV', 'ENXIO', 'ETIMEDOUT'):
            self.assertIn(error, fault)
        self.assertIn('damaged = true', fault)
        self.assertNotRegex(fault, r'bsp_sdcard_|sdmmc_')

    def test_browser_handles_close_even_on_allocation_failure(self):
        source = read('main/core/storage_browser.cpp')
        self.assertIn('std::unique_ptr<FILE,decltype(&fclose)>', source)
        self.assertIn('std::unique_ptr<DIR,decltype(&closedir)>', source)
        worker = part(source, 'void worker(void *arg)', 'std::shared_ptr<StorageJob> start')
        self.assertLess(worker.index('sd::Session card'), worker.index('execute(*job)'))
        self.assertLess(worker.index('execute(*job)'), worker.index('card.reportError(job->error)'))
        self.assertLess(worker.index('card.reportError(job->error)'), worker.index('job->done.store'))
        self.assertLess(worker.index('job->done.store'), worker.index('--active'))
        self.assertIn('if(job.error) { job.entries.clear();return; }', source)

    def test_old_bookmarks_and_delete_requests_cannot_cross_a_remount(self):
        storage = read('main/core/storage_browser.cpp')
        progress = part(storage, 'void progressWorker', 'bool accepted(')
        self.assertLess(progress.index('job->cardGeneration!=card.generation()'), progress.index('writeProgress(*job)'))
        worker = part(storage, 'void worker(void *arg)', 'std::shared_ptr<StorageJob> start')
        self.assertLess(worker.index('job->removeRecording &&'), worker.index('job->cardGeneration=card.generation()'))
        self.assertLess(worker.index('job->cardGeneration!=card.generation()'), worker.index('execute(*job)'))
        restore = part(storage, 'reading::Mark readProgress', 'void writeProgress')
        self.assertEqual(restore.count('cardGeneration==cardGeneration'), 2)
        reader = read('main/apps/text_reader.cpp')
        self.assertIn('m_cardGeneration=job->cardGeneration', reader)
        self.assertIn('saveTextProgress(m_openPath,m_progress,m_cardGeneration)', reader)
        recorder = read('main/apps/recorder.cpp')
        self.assertIn('filesGeneration=listing->cardGeneration', recorder)
        self.assertIn('deleteRecording(pendingDeletePath,pendingDeleteGeneration,pendingDeleteIdentity)', recorder)

    def test_recorder_keeps_lease_until_file_close_and_preserves_partial_files(self):
        source = read('main/core/audio_recorder.cpp')
        body = part(source, 'void execute(Job &job)', 'void worker(')
        self.assertLess(body.index('sd::Session card'), body.index('File file(card)'))
        self.assertNotIn('card.release()', body)
        file_owner = part(source, 'struct File {', 'struct Stream {')
        self.assertIn('std::fclose(p)', file_owner)
        self.assertIn('card.reportError(EIO)', file_owner)
        self.assertLess(body.index('std::fclose(file.p)'), body.index('std::rename('))
        self.assertNotRegex(body, r'\b(?:remove|unlink)\s*\(')
        self.assertIn('file.p = nullptr', body)

    def test_fc_releases_storage_after_loading_and_vault_uses_same_recovery(self):
        fc = part(read('main/core/fc_emulator.cpp'), 'void execute(Job &job)', 'void worker(')
        self.assertLess(fc.index('sd::Session card'), fc.index('std::fopen('))
        self.assertLess(fc.rindex('owner.reset()'), fc.index('loadMapping(job, card)'))
        self.assertLess(fc.index('loadMapping(job, card)'), fc.index('card.release()'))
        self.assertLess(fc.index('card.release()'), fc.index('job.ready.store'))
        vault = part(read('main/core/vault_connection.cpp'), 'bool ConnectionConfig::readSd(', '} // namespace vault')
        self.assertLess(vault.index('core::sd::Session card'), vault.index('std::fopen('))
        self.assertLess(vault.index('std::fclose(file)'), vault.index('return parse('))
        self.assertIn('card.reportError(EIO)', vault)

    def test_deep_sleep_still_waits_for_every_worker_before_bsp_unmount(self):
        power = read('main/core/power_mgr.cpp')
        flow = part(power, 'void Power::processStandby()', 'void Power::syncTouch()')
        for guard in ('uiStopped.load()', 'core::storageIdle()', 'vault::idle()', 'core::recorder::idle()', 'core::fc::idle()'):
            self.assertLess(flow.index(guard), flow.index('bsp_sdcard_unmount()'))
        session = part(read('main/core/sd_media.cpp'), 'Session::Session(Access access,bool recover,bool drain)', 'Session::~Session()')
        self.assertLess(session.index('sys::Power::sleepTerminal()'), session.index('bsp_sdcard_get_handle()'))
        self.assertIn('sys::Power::WorkGuard work', session)


if __name__ == '__main__':
    unittest.main(verbosity=2)
