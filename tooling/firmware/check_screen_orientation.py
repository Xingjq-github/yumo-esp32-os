"""Screen-orientation integration contracts; no compiler or device access."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]/'firmware'
def read(path):return (ROOT/path).read_text(encoding='utf-8-sig')

class ScreenOrientationContracts(unittest.TestCase):
    def test_panel_only_rotation_keeps_logical_capture_coordinates(self):
        source=read('main/core/screen_orientation.cpp')
        self.assertIn('bsp_display_set_orientation(unsigned(target))',source)
        self.assertNotIn('lv_display_set_rotation',source)
        self.assertNotIn('transform_rotation',source)
        board=read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        for token in ('esp_lcd_panel_swap_xy','esp_lcd_panel_mirror','esp_lcd_panel_set_gap',
                      'orientation_commands(panel_orientation)','.process_coordinates = orientation_touch'):
            self.assertIn(token,board)
        for capture in ('screenshot','display_cast'):
            source=read('main/core/'+capture+'.cpp')
            self.assertIn('lv_display_get_rotation(display)!=LV_DISPLAY_ROTATION_0',source)
            self.assertIn('ymcast_copy_rect',source)

    def test_common_switch_controls_orientation_and_game_consumers(self):
        self.assertIn('ServiceAction::Motion,!sys::Settings::motionSensorOn()',read('main/ui/quick_panel.cpp'))
        self.assertIn('self->request(sys::ServiceAction::Motion,on)',read('main/apps/settings_system.cpp'))
        qmi=read('main/core/qmi8658.cpp')
        self.assertIn('return systemPermission && (s_inUse.load() || s_temporarySession.load',qmi)
        source=read('main/core/screen_orientation.cpp')
        self.assertIn('Settings::motionSensorOn()',source)
        self.assertIn('if(!wanted || !enabled) {filter.reset();return;}',source)
        self.assertNotIn('setMotionSensorOn',source)

    def test_gravity_apps_lock_at_entry_and_map_axes_before_gameplay(self):
        self.assertIn('m_active && m_active->locksScreenOrientation()',read('main/ui/window_mgr.hpp'))
        for app in ('gravity_maze','fc_player'):
            self.assertIn('locksScreenOrientation() const override {return true;}',read('main/apps/'+app+'.hpp'))
            source=read('main/apps/'+app+'.cpp')
            self.assertIn('ScreenOrientation::mapAcceleration(sample.value)',source)
        main=read('main/main.cpp')
        self.assertLess(main.index('WindowManager::instance().processNavigation()'),
                        main.index('ScreenOrientation::poll(WindowManager::instance().locksScreenOrientation())'))

    def test_touch_sleep_and_capture_defer_rotation(self):
        source=read('main/core/screen_orientation.cpp')
        for gate in ('Power::isScreenOn()','!Power::sleepPending()','!Power::slowIdle()',
                     'LV_INDEV_STATE_PRESSED','lv_indev_get_scroll_obj(touch)',
                     'bsp_power_touch_pending()','!Screenshot::idle()'):
            self.assertLess(source.index(gate),source.index('bsp_display_set_orientation'))
        self.assertIn('bsp_power_touch_reset()',source)

    def test_sampling_is_worker_only_and_caches_are_invalidated(self):
        gui=read('main/core/screen_orientation.cpp')
        self.assertIn('Qmi8658::cachedAcceleration(sample)',gui)
        self.assertNotRegex(gui,r'readAcceleration\(|i2c_master_|pollTemporaryAcceleration\(')
        qmi=read('main/core/qmi8658.cpp')
        self.assertIn('if((!session && !s_inUse.load()) || !enabled()) return;',qmi)
        self.assertIn('s_sampleValid=false',qmi)
        self.assertIn('std::try_to_lock',qmi.split('bool Qmi8658::cachedAcceleration',1)[1])

    def test_reboot_defaults_and_clock_has_no_local_rotation(self):
        source=read('main/core/screen_orientation.cpp')
        self.assertIn('filter=orientation::Filter{}',source)
        self.assertNotRegex(source,r'nvs_|RTC_DATA_ATTR|Settings::set')
        self.assertIn('c.bluetooth=true;c.wifi=true;c.motion=false',read('main/core/settings_store.cpp'))
        clock=read('main/apps/watchface.cpp')
        self.assertNotRegex(clock,r'Qmi8658|updateOrientation|applyOrientation|transform_rotation')

if __name__=='__main__':unittest.main()
