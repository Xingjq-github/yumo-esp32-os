"""Safe-eject source contracts and specification model, without compilation.

The model exercises admission schedules, NOT firmware/FatFs or electrical safety.
Physical removal/reinsertion and error injection still require device testing.
"""
from pathlib import Path
import unittest

ROOT = (Path(__file__).resolve().parents[2]/'firmware')


def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


def part(s, a, b):
    return s.split(a, 1)[1].split(b, 1)[0]


class MediaModel:
    def __init__(self):
        self.state = 'available'
        self.epoch = 1
        self.users = 0
        self.fault = False
        self.mounted = True

    def token(self, resume=False):
        return (0 if self.state in ('ejecting', 'reading') else self.epoch, resume)

    def eject(self):
        if self.state == 'reading':
            return False
        if self.state not in ('ejected', 'ejecting'):
            self.epoch += 1
            self.state = 'ejecting'
        return True

    def acquire(self, token, drain=False, read_ok=True):
        epoch, resume = token
        finishing = drain and self.state == 'ejecting' and epoch == self.epoch - 1
        if not epoch or (epoch != self.epoch and not finishing):
            return False
        if self.state == 'reading' or (self.state == 'ejecting' and not finishing):
            return False
        if finishing and (not self.mounted or self.fault):
            return False
        if self.state in ('ejected', 'failed'):
            if not resume or self.users or not read_ok:
                return False
            self.state = 'available'
            self.mounted = True
            self.fault = False
        self.users += 1
        return True

    def poll(self, idle=True, timeout=False, ready=True, unmount=True, pins=True):
        if self.state != 'ejecting':
            return
        if timeout:
            self.state = 'failed'
        elif self.users or not idle:
            return
        elif self.fault or not ready or not unmount or not pins:
            self.state = 'failed'
        else:
            self.mounted = False
            self.state = 'ejected'


class EjectSchedules(unittest.TestCase):
    def test_current_handles_finish_before_success(self):
        m = MediaModel()
        self.assertTrue(m.acquire(m.token()))
        m.eject(); m.poll()
        self.assertEqual(m.state, 'ejecting')
        m.users -= 1
        m.poll(idle=False)
        self.assertTrue(m.mounted)
        m.poll()
        self.assertEqual(m.state, 'ejected')

    def test_old_queued_reads_cannot_reopen_after_safe_label(self):
        m = MediaModel(); old = m.token(True)
        m.eject(); refused = m.token(True); m.poll()
        for token in (old, refused):
            self.assertFalse(m.acquire(token))
        self.assertFalse(m.mounted)

    def test_only_previously_submitted_bookmarks_can_drain(self):
        m = MediaModel(); mark = m.token(); m.eject()
        self.assertFalse(m.acquire(mark))
        self.assertTrue(m.acquire(mark, drain=True))
        self.assertFalse(m.acquire(m.token(), drain=True))
        m.users -= 1; m.poll()
        self.assertFalse(m.acquire(mark, drain=True))

    def test_no_resume_by_write_or_periodic_poll(self):
        m = MediaModel(); m.eject(); m.poll()
        for _ in range(100):
            m.poll()
        self.assertFalse(m.acquire(m.token(False)))
        self.assertEqual(m.state, 'ejected')

    def test_explicit_read_only_restores_on_success(self):
        m = MediaModel(); m.eject(); m.poll()
        self.assertFalse(m.acquire(m.token(True), read_ok=False))
        self.assertEqual(m.state, 'ejected')
        self.assertTrue(m.acquire(m.token(True)))
        self.assertEqual(m.state, 'available')

    def test_error_timeout_or_incomplete_teardown_never_says_safe(self):
        for fault in ('timeout', 'ready', 'unmount', 'pins', 'write'):
            with self.subTest(fault=fault):
                m = MediaModel(); m.eject()
                args = {}
                if fault == 'write': m.fault = True
                else: args[fault] = fault == 'timeout'
                m.poll(**args)
                self.assertEqual(m.state, 'failed')

    def test_repeated_eject_is_idempotent_and_reading_refuses_it(self):
        m = MediaModel(); m.eject(); epoch = m.epoch
        m.eject(); self.assertEqual(m.epoch, epoch)
        m.poll(); m.eject(); self.assertEqual(m.state, 'ejected')
        m.state = 'reading'; self.assertFalse(m.eject())


class EjectSourceContracts(unittest.TestCase):
    def test_ui_order_async_read_and_electrical_notice(self):
        s = read('main/apps/settings_system.cpp')
        self.assertLess(s.index('v.sdEject=c::button'), s.index('c::button(v.body,"深度睡眠"'))
        for text in ('安全弹出 SD 卡', '可插拔SD卡', '卡槽仍供电', '正在保存并停止读写，请勿拔卡', 'core::sd::ejectError()'):
            self.assertIn(text, s)
        self.assertIn('core::listDirectory(core::sd::Root,false)', s)
        self.assertIn('c::disabled(v.sdEject,core::sd::busy() || bool(sdRead))', s)
        self.assertNotRegex(s, r'bsp_sdcard_|sdmmc_|fopen\(')
        self.assertIn('sdRead->cancelled.store(true)', read('main/apps/settings.cpp'))

    def test_request_is_immediate_and_publishes_epoch_with_state(self):
        s = part(read('main/core/sd_media.cpp'), 'bool requestEject()', 'const char *ejectError()')
        self.assertIn('control.compare_exchange_strong(value,next|uint32_t(State::Ejecting))', s)
        self.assertNotRegex(s, r'mutex|bsp_sdcard_|vTaskDelay|xTaskCreate')
        self.assertIn('stateOf(value)==State::Reading', s)

    def test_poll_waits_and_checks_every_stage(self):
        s = part(read('main/core/sd_media.cpp'), 'void pollEject()', 'Session::Session(')
        ordered = ['std::try_to_lock', 'users || !core::storageIdle() || !core::recorder::idle()',
                   'if(damaged)', 'bsp_sdcard_ready()', 'bsp_sdcard_unmount()', 'bsp_sdcard_quiesce()',
                   'withState(value,State::Ejected)']
        positions = [s.index(v) for v in ordered]
        self.assertEqual(positions, sorted(positions))
        for token in ('10000000', 'Failure::Timeout', 'ESP_ERR_NOT_FINISHED', 'Failure::Unmount', 'Failure::Pins'):
            self.assertIn(token, s)
        self.assertNotRegex(s, r'while\s*\(|for\s*\(|bsp_sdcard_mount|vTaskDelay')

    def test_card_ready_checks_r1_and_no_power_rail_is_touched(self):
        s = read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        ready = part(s, 'esp_err_t bsp_sdcard_ready(', 'esp_err_t bsp_sdcard_quiesce(')
        for token in ('MMC_SEND_STATUS', '.timeout_ms = 100', 'cmd.error', 'cmd.response[0] & errors',
                      'MMC_R1_READY_FOR_DATA', 'MMC_R1_CURRENT_STATE_TRAN'):
            self.assertIn(token, ready)
        quiet = part(s, 'esp_err_t bsp_sdcard_quiesce(', 'static void bsp_codec_release(')
        for pin in ('CLK', 'CMD', 'D0', 'D1', 'D2', 'D3'):
            self.assertIn('1ULL << BSP_SD_' + pin, quiet)
        for token in ('GPIO_MODE_DISABLE', 'GPIO_PULLUP_DISABLE', 'GPIO_PULLDOWN_DISABLE', 'if (bsp_sdcard) return ESP_ERR_INVALID_STATE'):
            self.assertIn(token, quiet)
        self.assertNotRegex(quiet, r'gpio_set_level|BAT_EN|POWER|sdmmc_')

    def test_resume_fences_old_jobs_and_checks_root_before_available(self):
        s = part(read('main/core/sd_media.cpp'), 'Session::Session(', 'Session::~Session()')
        for token in ('access.epoch!=epoch && !finishing', 'drain && current==State::Ejecting',
                      '!access.resume || !recover || users', 'Failure::Unmount', 'struct ResumeGuard',
                      'control.store(original)', 'opendir(Root)', 'closedir(root)'):
            self.assertIn(token, s)
        self.assertLess(s.index('if(readError)'), s.index('control.store(withState(value,State::Available))'))
        self.assertLess(s.index('sys::Power::sleepTerminal()'), s.index('bsp_sdcard_get_handle()'))

    def test_every_async_consumer_captures_admission_before_launch(self):
        for path in ('storage_browser.cpp', 'audio_recorder.cpp', 'fc_emulator.cpp', 'password_vault.cpp'):
            s = read('main/core/' + path)
            self.assertIn('sdAccess=', s, path)
            self.assertIn('sd::requestAccess(', s, path)
        self.assertIn('readSd(fromSd,job.error,job.sdAccess)', read('main/core/password_vault.cpp'))
        self.assertIn('core::sd::Session card(access)', read('main/core/vault_connection.cpp'))

    def test_app_exit_owns_cancellation_and_auto_completion_cannot_resume_card(self):
        s = read('main/core/audio_recorder.cpp')
        self.assertIn('while (!job.stop.load() && !sys::Power::sleepPending())', s)
        self.assertNotIn('sd::stopping()', s)
        for name in ('audio_recorder.cpp', 'fc_emulator.cpp'):
            body = part(read('main/core/' + name), 'void execute(Job &job)', 'void worker(')
            self.assertLess(body.index('if(job.stop.load() || sys::Power::sleepPending()) return;'),
                            body.index('sd::Session card'))
            after_mount = body.split('sd::Session card', 1)[1]
            self.assertIn('if(job.stop.load() || sys::Power::sleepPending()) return;', after_mount)
        for name in ('recorder', 'fc_player'):
            hide = part(read('main/apps/' + name + '.cpp'), '::onHide()', '::onDestroy()')
            self.assertIn('job->requestStop()', hide)
            self.assertIn('listing->cancelled.store(true)', hide)
        self.assertIn('if (!checkpoint(file.p, total)) {\n          card.reportError(EIO);', s)
        # Preserve the real failure: EEXIST is not media damage and must not
        # force a remount; actual I/O errors still reach the SD fault handler.
        self.assertIn('if (std::rename(temporary.c_str(), job.path.c_str()) != 0) {\n      card.reportError(errno);', s)
        poll = part(read('main/apps/recorder.cpp'), 'void Recorder::poll()', 'bool Recorder::onBack()')
        self.assertEqual(poll.count('refresh();'), 2)
        self.assertEqual(poll.count('if(core::sd::state()==core::sd::State::Available) refresh();'), 1)
        self.assertIn('if(!recordAfterPlayback&&pendingDeletePath.empty()&&core::sd::state()==core::sd::State::Available) refresh();',poll)

    def test_service_reuses_worker_and_sleep_waits(self):
        self.assertIn('core::sd::pollEject();', read('main/core/system_service.cpp'))
        s = read('main/core/sd_media.cpp')
        self.assertNotRegex(s, r'xTaskCreate|lv_timer_create|esp_timer_create')
        flow = part(read('main/core/power_mgr.cpp'), 'void Power::processStandby()', 'void Power::syncTouch()')
        self.assertLess(flow.index('if(core::sd::busy()) return'), flow.index('bsp_sdcard_unmount()'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
