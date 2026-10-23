"""FC input integration contracts; runtime behavior still needs device acceptance."""
from pathlib import Path
import re,unittest
from check_menu_layout import font_metrics
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def part(text,start,end):return text.split(start,1)[1].split(end,1)[0]

class FcControlsContracts(unittest.TestCase):
    def test_offline_start_resume_and_worker_have_no_keyboard_gate(self):
        source=read('main/apps/fc_player.cpp')
        play=part(source,'void FcPlayer::play','void FcPlayer::createControls')
        self.assertNotIn('if (!sys::BleHid::keyboardConnected())',play)
        self.assertIn('core::fc::start(files[index].path,keySession)',play)
        resume=part(source,'void FcPlayer::resumeGame','void FcPlayer::stopGame')
        self.assertNotIn('keyboardConnected',resume)
        self.assertIn('!sys::Power::isScreenOn()',resume)
        worker=read('main/core/fc_emulator.cpp')
        self.assertNotRegex(worker,r'BleHid|hasKeyboard')
        self.assertIn('if (!sys::Power::isScreenOn())',worker)

    def test_empty_keyboard_reports_do_not_clear_local_input_every_poll(self):
        keys=part(read('main/apps/fc_player.cpp'),'void FcPlayer::keys','void FcPlayer::poll')
        self.assertIn('if(self->keyboardLinked!=linked)',keys)
        empty=part(keys,'if(!linked) {','auto effective=keys;')
        self.assertNotRegex(empty,r'clearInput\(|touch\.clear|clearGameKeys|job->buttons')
        self.assertIn('return;',empty)
        self.assertIn('self->blockedKeys.bits[i]&=keys.bits[i]',keys)
        self.assertIn('self->sendInput();self->previous=effective',keys)

    def test_game_owns_press_edges_without_app_pointer_in_button_task(self):
        source=read('main/core/button_mgr.cpp')
        self.assertNotRegex(source,r'\blv_\w+\s*\(|FcPlayer|WindowManager')
        self.assertIn('routeGameKey(ev,GameKeyState::Boot)',source)
        self.assertIn('routeGameKey(ev,GameKeyState::Key)',source)
        self.assertIn('BUTTON_PRESS_DOWN,BUTTON_PRESS_UP,BUTTON_PRESS_END',source)
        self.assertIn('portENTER_CRITICAL(&s_gameLock)',source)
        worker=read('main/core/fc_emulator.cpp')
        self.assertIn('sys::Buttons::takeGameKeys(job.keySession)',worker)
        self.assertIn('sys::Buttons::gameRunning(job.keySession,false)',worker)
        self.assertIn('job.pressed.exchange(0)',worker)

    def test_hidden_touch_zones_and_fixed_translucent_joystick(self):
        source=read('main/apps/fc_player.cpp')
        create=part(source,'void FcPlayer::createControls','void FcPlayer::updateStick')
        self.assertNotRegex(create,r'c::button|c::label|LV_SYMBOL')
        for token in ('LV_OPA_TRANSP','lv_obj_set_press_lock','lv_obj_set_gesture_bubble',
                      'padui::StickX-padui::Radius','padui::StickY-padui::Radius','LV_OPA_20','LV_OPA_40'):
            self.assertIn(token,create)
        touch=part(source,'void FcPlayer::touchEvent','void FcPlayer::sendInput')
        self.assertIn('LV_EVENT_PRESS_LOST',touch)
        self.assertIn('self->touch.clear()',touch)
        self.assertIn('lv_indev_wait_release(indev);self->showMenu()',touch)

    def test_home_and_gestures_cannot_exit_during_game(self):
        self.assertIn('if(!QuickPanel::back() && !sys::Buttons::blockHomeShortcut())',read('main/ui/window_mgr.cpp'))
        header=read('main/apps/fc_player.hpp');source=read('main/apps/fc_player.cpp')
        self.assertIn('handlesHorizontalGesture() const override { return true; }',header)
        self.assertIn('if(!indev || self->page==Page::Game) return',source)
        self.assertIn('sys::Buttons::gameMode(keySession,true)',source)
        self.assertIn('sys::Buttons::gameMode(keySession,false)',source)
        self.assertIn('static_cast<FcPlayer *>(lv_event_get_user_data(e))->stopGame(true)',source)

    def test_stop_waits_for_worker_and_exit_returns_resources(self):
        source=read('main/apps/fc_player.cpp')
        done=part(source,'if (job->done.load(std::memory_order_acquire))','if (!job->ready.load')
        self.assertLess(done.index('job.reset()'),done.index('if(leaveApp)'))
        hide=part(source,'void FcPlayer::onHide','void FcPlayer::onDestroy')
        for token in ('releaseGameKeys(keySession);keySession=0','clearInput()',
                      'ExternalKeyboard::capture(nullptr, nullptr)','job->requestStop()','listing->cancelled.store(true)'):
            self.assertIn(token,hide)
        self.assertIn('EventBus::unsubscribe(screenOffSub)',source)
        self.assertIn('lv_timer_delete(timer)',source)

    def test_menu_text_and_targets_fit_the_real_screen(self):
        metrics=font_metrics('main/ui/lv_font_cjk_16.c')
        for text in ('游戏菜单 · 蓝牙键盘','空格继续，Esc返回列表','KEY=A  BOOT=B',
                     '下左选择，下右开始','继续游戏','返回游戏列表','退出应用'):
            self.assertLessEqual(sum((metrics[ord(ch)]['adv_w']+8)//16 for ch in text),232,text)
        for top in (58,102,146,190):self.assertLessEqual(top+40,240)
        for text in ('重力 开','重力 关','摇杆:8F','摇杆:4F','返回游戏列表','退出应用'):
            self.assertLessEqual(sum((metrics[ord(ch)]['adv_w']+8)//16 for ch in text),107,text)

if __name__=='__main__':unittest.main(verbosity=2)
