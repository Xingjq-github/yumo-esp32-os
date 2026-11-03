"""HID/UI source contracts and bounded behavior models; not hardware tests.
The actual C++ key model also has a separate compiler-executed constexpr suite.
"""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BACKUP=ROOT/'../tooling/firmware/fixtures/contracts/hid-ui-20260917-001837'
def read(p): return (ROOT/p).read_text(encoding='utf-8-sig')
def part(s,a,b): return s.split(a,1)[1].split(b,1)[0]
class HidUiContracts(unittest.TestCase):
    def test_app_registry_has_one_hid_no_stopwatch(self):
        names=re.findall(r'registerApp\(\s*"([^"]+)"',read('main/main.cpp'))
        self.assertEqual(names,['时钟','手电筒','设置','重力球','蓝牙键鼠','蓝牙音乐','文本','相册','触控展示','密码本'])
        sources=read('main/CMakeLists.txt')
        for name in ('bt_keyboard.cpp','bt_mouse.cpp','stopwatch.cpp'): self.assertNotIn('"apps/'+name+'"',sources)
        self.assertIn('"apps/bt_keyboard_mouse.cpp"',sources);self.assertIn('"apps/touch_scope.cpp"',sources)
    def test_six_removed_files_stay_unregistered_after_authorized_cleanup(self):
        baseline=json.loads((BACKUP/'manifest.json').read_text(encoding='utf-8'))['source_before']
        cmake=read('main/CMakeLists.txt')
        for app in ('bt_keyboard','bt_mouse','stopwatch'):
            for ext in ('cpp','hpp'):
                name=f'main/apps/{app}.{ext}'
                self.assertEqual(len(baseline[name]['sha256']),64)
                self.assertFalse((ROOT/name).exists())
                self.assertNotIn('"'+name.removeprefix('main/')+'"',cmake)
                self.assertFalse((BACKUP/'removed'/name).exists())
    def test_radios_power_storage_codec_and_other_components_unchanged(self):
        baseline=json.loads((BACKUP/'manifest.json').read_text(encoding='utf-8'))['source_before']
        allowed={'main/core/connectivity.cpp','main/core/connectivity.hpp','main/core/settings_store.cpp','main/core/settings_store.hpp','main/core/qmi8658.cpp','main/core/qmi8658.hpp','main/core/storage_browser.cpp','main/core/ble_hid.cpp','main/core/ble_hid.hpp','main/core/system_service.cpp','main/core/system_service.hpp','main/core/battery_mgr.cpp','main/core/battery_estimator.hpp','components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c','components/esp32_s3_touch_lcd_1_54/include/bsp/touch_observer.h'}
        # Network drive is removed; PWR/button/audio behavior has dedicated cleanup contracts.
        allowed.update({'main/core/file_server.cpp','main/core/file_server.hpp','main/core/power_mgr.cpp',
                        'main/core/button_mgr.cpp','main/core/audio_mgr.cpp'})
        from check_netdisk_cleanup import verify_power_and_buttons_preserved
        verify_power_and_buttons_preserved(self)
        protected=[p for p in baseline if (p.startswith('main/core/') or p.startswith('components/')) and p not in allowed]
        protected+=['dependencies.lock','main/idf_component.yml','main/ui/lv_font_cjk_16.c','main/ui/home_screen.cpp']
        for p in protected:
            if p in baseline: self.assertEqual(hashlib.sha256((ROOT/p).read_bytes()).hexdigest(),baseline[p]['sha256'],p)
    def test_neutral_pressed_and_checked_button_states(self):
        s=read('main/ui/hid_controls.hpp')
        for token in ('theme::surface()','theme::accent(),LV_STATE_PRESSED','theme::accent(),LV_STATE_CHECKED','LV_STATE_DISABLED','transform_width(obj,0','transform_height(obj,0'):
            self.assertIn(token,s)
        for name in ('bt_keyboard_mouse','bt_music'): self.assertIn('hid_controls.hpp',read('main/apps/'+name+'.cpp'))
    def test_shared_shell_survives_mode_switch(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        shell=part(s,'void BtKeyboardMouse::showKeyboard','void BtKeyboardMouse::switchCb')
        self.assertIn('cancelPending();releasePointer()',shell)
        self.assertNotIn('m_status,LV_OBJ_FLAG_HIDDEN',shell)
        self.assertNotIn('m_switch,LV_OBJ_FLAG_HIDDEN',shell)
        self.assertIn('LV_SYMBOL_KEYBOARD,switchCb,this,3,0,65,44',s)
        self.assertIn('196,45,44,195',s)
        self.assertNotIn('stateText',s)
    def test_all_touch_grids_fit_240_and_have_36px_targets(self):
        rectangles=[(3,0,65,44),(196,45,44,195)]
        rectangles += [(69+i*57,0,56,44) for i in range(3)]
        rectangles += [((i%3)*80,45+(i//3)*49,79,48) for i in range(12)]
        rectangles += [((i%3)*80,76+(i//3)*41,79,40) for i in range(12)]
        rectangles += [((i%3)*80,139 if i<3 else 198,79,58 if i<3 else 42) for i in range(6)]
        for x,y,w,h in rectangles:
            self.assertGreaterEqual(min(w,h),36);self.assertGreaterEqual(min(x,y),0)
            self.assertLessEqual(x+w,240);self.assertLessEqual(y+h,240)
        s=read('main/apps/bt_keyboard_mouse.cpp')
        for token in ('i*57,0,56,44','(i%3)*80,(i/3)*49,79,48','31+(i/3)*41,79,40','&lv_font_montserrat_14'):
            self.assertIn(token,s)
    def test_character_compose_commits_once_and_cancels_on_switch(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        cb=part(s,'void BtKeyboardMouse::keyCb','void BtKeyboardMouse::refreshPreview')
        for token in ('m_compose.due(now)','!self->commitPending()','m_compose.begin','keys::Alphabet::Numbers','if(self->m_compose.pending) self->cancelPending()','sendKey(0x2a)'):
            self.assertIn(token,cb)
        self.assertIn('selectTab(0)',s)
        self.assertIn('DelayMs=700',read('main/apps/hid_keyboard_model.hpp'))
        self.assertIn('coversAscii()',read('../tooling/firmware/hid_keyboard_model_test.cpp'))
    def test_model_all_ascii_and_shortcuts_have_direct_cpp_tests(self):
        s=read('../tooling/firmware/hid_keyboard_model_test.cpp')
        for token in ('asciiUnique()','allModesCoverAscii()','cycleAndCancel()','wraps()','sequence()','shortcuts[4].mac,{0x14,Ctrl|Gui}'):
            self.assertIn(token,s)
    def test_mouse_acceleration_reports_preserved(self):
        before=(BACKUP/'source-before/main/apps/bt_mouse.cpp').read_text(encoding='utf-8')
        after=read('main/apps/bt_keyboard_mouse.cpp')
        for name in ('kMoveGainBase','kClickThreshold','kAccelFactor','kAccelCap','kWheelStep','kTapMaxMs','kDragWindowMs','kDragMoveStart'):
            pattern=rf'constexpr \w+ {name} = ([^;]+);'
            self.assertEqual(re.search(pattern,before).group(1),re.search(pattern,after).group(1))
        # Mouse performance update keeps gain/fractions but submits one complete
        # delta. The NimBLE worker, not a GUI loop, now splits HID int8 reports.
        b=part(after,'bool BtKeyboardMouse::emitMove','void BtKeyboardMouse::touchpadCb')
        for token in ('m_accX +=','m_accY +=','m_accX -=','m_accY -=','sendMouse(moveX, moveY, 0, buttons, m_pointerLink)'):
            self.assertIn(token,b)
        self.assertNotIn('while (',b)
        queue=read('main/core/hid_mouse_queue.hpp')
        self.assertIn('std::max(-127,std::min(127,value))',queue)
        replay=read('../tooling/firmware/native/ble_mouse_ui_replay.cpp')
        self.assertIn('sum(0)==896 && sum(1)==672',replay)
        self.assertIn('lv_indev_read(touch)',replay)
    def test_drag_release_and_autoclick_lifetimes(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        release=part(s,'void BtKeyboardMouse::releasePointer','void BtKeyboardMouse::cancelPending')
        self.assertIn('sys::BleHid::cancelMouse()',release)
        self.assertIn('m_pointerLink=m_clickLink=0',release)
        self.assertIn('LV_EVENT_PRESS_LOST',s)
        hidden=part(s,'void BtKeyboardMouse::onHide','void BtKeyboardMouse::onDestroy')
        self.assertLess(hidden.index('m_visible=false'),hidden.index('releasePointer()'))
        self.assertIn('cancelPending();releasePointer()',hidden)
        self.assertIn('BleHid::cancelKeys()',hidden)
        auto=part(s,'void BtKeyboardMouse::autoClickTimerCb','void BtKeyboardMouse::statusTimerCb')
        self.assertIn('!self->m_keyboard',auto);self.assertIn('self->m_visible',auto)
        self.assertNotIn('isScreenOn()',auto) # existing intentional dark autoclick remains
    def test_media_controls_are_real_hid_not_fake_playback(self):
        s=read('main/apps/bt_music.cpp')
        for name in ('PreviousTrack','PlayPause','NextTrack','VolumeDown','Mute','VolumeUp'):self.assertIn('ConsumerKey::'+name,s)
        self.assertIn('SystemService::sendConsumer(item->key)',s)
        self.assertNotIn('vTaskDelay',s)
        for token in ('lv_slider_create','m_playing','trackName','progressTimer'):self.assertNotIn(token,s)
    def test_clock_has_year_and_four_faces_with_fixed_digital_layout(self):
        s=read('main/apps/watchface.cpp')
        self.assertIn('kFaceCount = 4',s);self.assertIn('value.tm_year+1900',s)
        self.assertIn('"%04d.%02d.%02d"',s)
        layout=part(s,'void Watchface::layoutDigitalFace','void Watchface::changeFace')
        self.assertIn('targets[]={m_weekday,m_date,m_digits[0],m_digits[1],m_colon,m_digits[2],m_digits[3],m_seconds}',layout)
        self.assertNotIn('m_dial,',layout);self.assertNotIn('m_card,',layout)
        self.assertNotIn('transform_rotation',s)
        self.assertIn('if(now==m_lastRendered) return',s)
        self.assertIn('if(!self->m_visible) return',s)
    def test_touch_observer_only_wraps_original_read_once(self):
        s=read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        # With the retired visualizer gone, use the driver's original read path.
        factory=part(s,'esp_err_t bsp_touch_new(', '#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)')
        self.assertEqual(factory.count('esp_lcd_touch_new_i2c_cst816s('),1)
        self.assertIn('return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, ret_touch);',factory)
        for token in ('touch_observer','touch_original_read','->read_data =','get_xy('):self.assertNotIn(token,factory)
    def test_touch_uses_fixed_pool_and_no_bus_reads_in_app(self):
        # Touch visualizer retired; no application may enable its diagnostic observer.
        self.assertFalse((ROOT/'main/apps/touch_scope.cpp').exists())
        for p in (ROOT/'main/apps').glob('*.cpp'):
            self.assertNotIn('bsp_touch_observer_enable(',p.read_text(encoding='utf-8'),p.name)
    def test_touch_black_idle_expiry_and_real_error_no_stale_coords(self):
        self.assertFalse((ROOT/'main/apps/touch_scope.hpp').exists())
        self.assertNotIn('touch_scope',read('main/CMakeLists.txt'))
        self.assertNotIn('TouchScope',read('main/main.cpp'))
    def test_touch_observer_is_disabled_on_hide_and_screenoff(self):
        # No diagnostic locks or snapshots remain in the per-poll touch path.
        s=read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        self.assertNotIn('touch_observer',s)
        self.assertNotIn('bsp_touch_observer_enable(true)',read('main/main.cpp'))
        for p in (ROOT/'main').rglob('*.cpp'):
            self.assertNotIn('bsp_touch_observer_enable(true)',p.read_text(encoding='utf-8'),p.name)
    def test_touch_public_snapshot_header_is_independent_of_board_typedefs(self):
        self.assertFalse((ROOT/'components/esp32_s3_touch_lcd_1_54/include/bsp/touch_observer.h').exists())
        self.assertFalse((ROOT/'main/apps/touch_scope.hpp').exists())
    def test_new_calls_exist_in_installed_lvgl_headers(self):
        headers='\n'.join(p.read_text(encoding='utf-8',errors='replace') for p in (ROOT/'../dependencies/managed_components/lvgl__lvgl').rglob('*.h'))
        for p in ('main/apps/bt_keyboard_mouse.cpp','main/apps/bt_music.cpp','main/apps/calendar.cpp','main/apps/dino.cpp','main/apps/gravity_maze.cpp','main/ui/hid_controls.hpp'):
            for call in set(re.findall(r'\b(lv_\w+)\s*\(',read(p))):self.assertRegex(headers,rf'\b{call}\b',call)
    def test_new_marker_and_no_config_or_dependency_bump(self):
        for name in ('main/main.cpp','main/apps/settings_system.cpp'):self.assertIn('BATTERY-CLEAN-20260917',read(name))
        # Central enables the SDK GATT client; all other configuration is preserved.
        from check_keyboard_battery import validate_config
        validate_config(self)
if __name__=='__main__':unittest.main(verbosity=2)
