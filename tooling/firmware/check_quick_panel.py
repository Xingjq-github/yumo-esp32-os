"""Integration boundaries not exercised by the fake-service LVGL replay."""
from pathlib import Path
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name):return (ROOT/name).read_text(encoding='utf8')

class QuickPanelContracts(unittest.TestCase):
    def test_scene_policy_keeps_touch_driven_apps_and_gameplay_reserved(self):
        for name in ('bt_keyboard_mouse','flashlight'):
            self.assertIn('allowsQuickPanel() const override {return false;}',read('main/apps/'+name+'.hpp'))
        for name,condition in (('photo_viewer','!m_viewing'),('dino','paused'),('gravity_maze','!active')):
            self.assertIn('allowsQuickPanel() const override {return '+condition+';}',read('main/apps/'+name+'.hpp'))
        fc=read('main/apps/fc_player.hpp')
        for condition in ('page!=Page::Game','job->paused.load()','lv_obj_is_hidden','!saveBusy'):self.assertIn(condition,fc)
        self.assertIn('virtual bool allowsQuickPanel() const {return true;}',read('main/ui/app.hpp'))

    def test_overlay_preserves_app_capture_and_hardware_ownership(self):
        source=read('main/ui/external_keyboard.cpp')
        scope=source.split('void ExternalKeyboard::rescope()',1)[1].split('void ExternalKeyboard::capture',1)[0]
        self.assertNotIn('rawHandler=',scope)
        for token in ('lv_indev_reset','lv_group_remove_all_objs','ignoreUntilRelease','KeyboardInput::snapshot'):self.assertIn(token,scope)
        buttons=read('main/core/button_mgr.cpp')
        route=buttons.split('static bool routePanelKey',1)[1].split('static void bootCb',1)[0]
        self.assertIn('if(!panel && !began)return false',route)
        self.assertIn('if(panel)',route)
        for callback,end in (('bootCb','keyCb'),('keyCb','pwrCb')):
            body=buttons.split('static void '+callback,1)[1].split('static void '+end,1)[0]
            self.assertLess(body.index('routeGameKey('),body.index('routePanelKey('))
        self.assertIn('!s_systemPanel.load(std::memory_order_acquire) && exit==session',buttons)

    def test_panel_teardown_precedes_app_teardown_and_filters_only_page_input(self):
        wm=read('main/ui/window_mgr.cpp')
        close=wm.split('void WindowManager::closeActive()',1)[1].split('void WindowManager::goBack',1)[0]
        self.assertLess(close.index('QuickPanel::close()'),close.index('m_active->onHide()'))
        panel=read('main/ui/quick_panel.cpp')
        poll=panel.split('void QuickPanel::poll()',1)[1]
        self.assertIn('if(!available()) {',poll)
        self.assertIn('if(visible() || !sys::Power::isScreenOn() || sys::Power::sleepPending())close();',poll)
        allow=panel.split('bool QuickPanel::allowEvent',1)[1].split('void QuickPanel::poll()',1)[0]
        self.assertIn('EventType::PreviousPage',allow);self.assertIn('EventType::NextPage',allow)
        self.assertNotIn('EventType::Screen',allow)
        self.assertNotRegex(panel,r'\b(?:xTaskCreate|nvs_set_\w+|esp_wifi_\w+)\s*\(')
        self.assertIn('TextInput::hide()',panel)
