"""FC tilt lifecycle/worker contracts. Actual tilt feel requires hardware acceptance."""
from pathlib import Path
import unittest
from check_menu_layout import font_metrics

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def part(text,start,end):return text.split(start,1)[1].split(end,1)[0]

class FcTiltContracts(unittest.TestCase):
    def test_temporary_permission_preserves_setting_and_fences_late_release(self):
        qmi=read('main/core/qmi8658.cpp')
        lease=part(qmi,'uint32_t Qmi8658::claimTemporaryUse()', 'void Qmi8658::pollTemporaryAcceleration()')
        self.assertIn('compare_exchange_strong(session,0',lease)
        self.assertIn('if(++generation==0) ++generation',lease)
        self.assertIn('systemPermission && (s_inUse.load() || s_temporarySession.load',lease)
        self.assertNotIn('Settings::',qmi)
        self.assertNotIn('setMotionSensorOn',read('main/apps/fc_player.cpp'))
        worker=read('main/core/system_service.cpp')
        self.assertIn('Qmi8658::requested(Settings::motionSensorOn()) && Power::isScreenOn() && !Power::slowIdle() && !Power::sleepPending()',worker)
        self.assertIn('if(motionWanted!=previousMotionWanted)',worker)
        self.assertIn('if(Qmi8658::setEnabled(motionWanted)) nextMotionAttempt=0',worker)

    def test_only_worker_samples_and_gui_cache_is_bounded(self):
        qmi=read('main/core/qmi8658.cpp')
        poll=part(qmi,'void Qmi8658::pollTemporaryAcceleration()', 'bool Qmi8658::temporaryAcceleration')
        self.assertIn('if((!session && !s_inUse.load()) || !enabled()) return',poll)
        self.assertIn('readAcceleration(sample.value,3)',poll)
        self.assertIn('if(session!=s_temporarySession.load',poll)
        cache=part(qmi,'bool Qmi8658::temporaryAcceleration', 'bool Qmi8658::enabled')
        self.assertIn('std::try_to_lock',cache)
        self.assertIn('s_sampleSession!=session',cache)
        self.assertNotIn('readAcceleration(',cache)
        self.assertIn('if(motionWanted) Qmi8658::pollTemporaryAcceleration()',read('main/core/system_service.cpp'))
        app=read('main/apps/fc_player.cpp')
        self.assertNotRegex(app,r'readAcceleration\(|pollTemporaryAcceleration\(|i2c_master_|xTaskCreate')
        self.assertEqual(app.count('lv_timer_create('),1)

    def test_sampling_only_in_live_game_and_stale_input_released(self):
        app=read('main/apps/fc_player.cpp')
        poll=part(app,'void FcPlayer::pollTilt()', 'void FcPlayer::touchEvent')
        claim=poll.index('claimTemporaryUse()')
        for gate in ('!tiltEnabled','!job->ready.load','job->paused.load()', 'job->stop.load()',
                     '!sys::Power::isScreenOn()', 'sys::Power::sleepPending()'):
            self.assertLess(poll.index(gate),claim)
        for token in ('tiltui::fresh(now,sample.timeMs)','sample.timeMs!=tiltSampleAt',
                      '!tiltui::fresh(now,tiltSampleAt)', 'tilt.clear();tiltReady=tiltSampleSeen=false',
                      'previousPad!=tilt.pad || previousReady!=tiltReady'):
            self.assertIn(token,poll)
        self.assertLess(poll.index('temporaryAcceleration(tiltSession,sample)'),poll.index('const auto now='))
        listed=part(app,'void FcPlayer::showList()', 'void FcPlayer::bluetoothNotice')
        self.assertNotIn('claimTemporaryUse',listed)
        self.assertNotIn('pollTilt()',listed)

    def test_pause_exit_and_page_transitions_release_before_teardown(self):
        app=read('main/apps/fc_player.cpp')
        for start,end in (('showList()', 'bluetoothNotice'), ('showBluetooth()', 'bluetoothAction'),
                          ('showMenu()', 'resumeGame'), ('onHide()', 'onDestroy')):
            body=part(app,'void FcPlayer::'+start, 'void FcPlayer::'+end)
            self.assertIn('releaseTilt()',body)
        hide=part(app,'void FcPlayer::onHide()', 'void FcPlayer::onDestroy')
        self.assertLess(hide.index('releaseTilt()'),hide.index('lv_timer_pause'))
        done=part(app,'if (job->done.load(std::memory_order_acquire))', 'if (!job->ready.load')
        self.assertLess(done.index('releaseTilt()'),done.index('job.reset()'))
        release=part(app,'void FcPlayer::releaseTilt()', 'void FcPlayer::pollTilt')
        self.assertIn('releaseTemporaryUse(tiltSession);tiltSession=0',release)
        self.assertIn('tilt.clear()',release)

    def test_composed_input_drives_stick_without_changing_game_worker(self):
        app=read('main/apps/fc_player.cpp')
        stick=part(app,'void FcPlayer::updateStick()', 'void FcPlayer::releaseTilt')
        for token in ('if(tiltEnabled || touch.fourWay)', 'lastPad&padui::Right', 'lastPad&padui::Up','int dx=touch.dx,dy=touch.dy'):
            self.assertIn(token,stick)
        send=part(app,'void FcPlayer::sendInput()', 'void FcPlayer::clearInput')
        self.assertIn('tiltui::compose(tiltEnabled,gravity,touch.pad',send)
        self.assertIn('padui::fourDirections(tilt.pad,tilt.x,tilt.y)',send)
        self.assertIn('touch.active==padui::Region::Stick,padui::combine(gamepadPad,keyboardPad)',send)
        self.assertLess(send.index('lastPad=pad'),send.index('updateStick()'))
        self.assertNotIn('Qmi8658',read('main/core/fc_emulator.cpp'))

    def test_default_off_and_controls_fit_existing_font_and_screen(self):
        self.assertIn('bool tiltEnabled=false',read('main/apps/fc_player.hpp'))
        app=read('main/apps/fc_player.cpp')
        self.assertIn('},this,78,36)',app)
        self.assertIn('lv_obj_set_pos(tiltToggle,119,0)',app)
        self.assertIn('c::checked(tiltToggle,tiltEnabled)',app)
        self.assertIn('ui::theme::accent(),LV_STATE_CHECKED',app)
        metrics=font_metrics('main/ui/lv_font_cjk_16.c')
        for text,width in (('重力 开',66),('重力 关',66),('FC游戏',73),('感应准备中',150),('感应不可用',150)):
            self.assertLessEqual(sum((metrics[ord(ch)]['adv_w']+8)//16 for ch in text),width,text)
        zones=((0,36),(42,73),(119,78),(198,42))
        for (x,w),(next_x,_) in zip(zones,zones[1:]):self.assertLessEqual(x+w,next_x)
        self.assertLessEqual(zones[-1][0]+zones[-1][1],240)

    def test_actual_sensor_configuration_is_verified_before_publishing_ready(self):
        source=read('main/core/qmi8658.cpp')
        init=part(source,'bool Qmi8658::init(void)', 'bool Qmi8658::available')
        self.assertLess(init.index('writeRegister(kCtrl2, AccelConfig)'),init.index('configured!=AccelConfig'))
        self.assertLess(init.index('configured!=AccelConfig'),init.index('s_ready = true'))
        self.assertIn('writeRegister(kCtrl3, 0x55)',init)
        model=read('../tooling/firmware/fc_tilt_test.cpp')
        self.assertIn('sys::Qmi8658::accelToG',model)
        self.assertIn('static_assert(rawSensorToDirections())',model)

if __name__=='__main__':unittest.main(verbosity=2)
