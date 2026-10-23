"""Offline regression contracts for the 240x240 input/power/torch refactor.
No device access. Complements (does not replace) real touch/current/wake tests.
"""
from pathlib import Path
import re
import unittest

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path): return (ROOT/path).read_text(encoding='utf-8-sig')

class UiPowerContracts(unittest.TestCase):
    def test_system_input_lifetime_and_routing(self):
        src=read('main/ui/text_input.cpp')
        for token in ('lv_indev_add_event_cb(pointer,pointerClick,LV_EVENT_CLICKED',
                      'lv_obj_check_type(field,&lv_textarea_class)',
                      'lv_dropdown_get_list(type)','lv_dropdown_get_list(character)',
                      'if(obj==viewport) viewport=nullptr','if(obj==target) { target=nullptr;',
                      'lv_obj_remove_event_cb(target,deleted)','lv_obj_set_height(viewport,viewportHeight)',
                      'lv_obj_scroll_to_view(field,LV_ANIM_OFF)'):
            self.assertIn(token,src)
        self.assertIn('TextInput::bindField(obj)',read('main/ui/compact_ui.cpp'))
        self.assertIn('TextInput::hide()',read('main/ui/window_mgr.cpp'))
        self.assertNotIn('inputTarget',read('main/apps/settings.hpp'))
        self.assertNotIn('退出',src)

    def test_input_geometry_two_rows_with_larger_touch_targets(self):
        src=read('main/ui/text_input.cpp')
        layout=read('main/ui/text_input_layout.hpp')
        self.assertIn('RowHeight=(BaseRowHeight*4+2)/3',layout)
        self.assertIn('Height=2*RowHeight+Gap',layout)
        self.assertIn('LV_FLEX_FLOW_COLUMN',src)
        self.assertIn('c::dropdown(selectors,',src)
        for name in ('删除','输入'):
            self.assertIn('c::button(actions,"'+name+'"',src)
        self.assertLess(src.index('c::button(actions,"删除"'),src.index('c::button(actions,"输入"'))
        self.assertIn('L::LeftWidth,L::RowHeight',src)
        self.assertIn('L::RightWidth,L::RowHeight',src)
        self.assertIn('#include "ui/text_input_layout.hpp"',read('../tooling/firmware/text_input_layout_test.cpp'))

    def test_home_font_has_every_registered_name_on_one_line(self):
        font=read('main/ui/lv_font_menu_14.c')
        codepoints=set()
        for start,length in re.findall(r'\.range_start = (\d+), \.range_length = (\d+)',font):
            codepoints.update(range(int(start),int(start)+int(length)))
        names=re.findall(r'registerApp\(\s*"([^"]+)"',read('main/main.cpp'))
        from check_menu_layout import font_metrics
        metrics=font_metrics('main/ui/lv_font_menu_14.c')
        for name in names:
            self.assertLessEqual(sum((metrics[ord(c)]['adv_w']+8)//16 for c in name),65,name)
            self.assertTrue(set(map(ord,name))<=codepoints,name)
        advances=[int(n)/16 for n in re.findall(r'\.adv_w = (\d+)',font)]
        self.assertLessEqual(4*max(advances),65)
        src=read('main/ui/home_screen.cpp')
        for token in ('kTileW,53','LV_FLEX_FLOW_ROW_WRAP','theme::fontMenu()',
                      'LV_LABEL_LONG_MODE_DOTS','kNameWidth','compact::button'):
            self.assertIn(token,src)
        self.assertIn('theme::surface()',read('main/ui/compact_ui.cpp'))
        # Reduce vertical blank space by a third, not the 1px sibling gap.
        self.assertLessEqual(abs((53-22)/(68-22)-2/3),0.02)
        self.assertGreaterEqual(53,36)

    def test_animation_is_single_shot_vector_and_reclaimed(self):
        src=read('main/ui/boot_animation.cpp')
        for token in ('if(played) return','elapsed>=boot::DurationMs','lv_trigo_cos',
                      'lv_label_set_text(wordmark,"YUMO")','boot::frame(elapsed)',
                      'lv_timer_delete(timer)','lv_obj_delete(cover)','if(!timer) stop()',
                      'wordmark=bar=nullptr'):
            self.assertIn(token,src)
        self.assertNotRegex(src,r'lv_(?:image|gif)|malloc|fopen')
        tick=src.split('void tick(',1)[1].split('void BootAnimation::start()',1)[0]
        self.assertNotRegex(tick,r'lv_\w+_create|lv_label_set_text|vTaskDelay')
        self.assertEqual(src.count('lv_timer_create('),1)
        model=read('main/ui/boot_animation_model.hpp')
        self.assertIn('DurationMs = 600, FrameMs = 15',model)
        self.assertIn('theme::bg()',src)
        self.assertIn('theme::accent()',src)
        self.assertIn('lv_obj_set_clickable(cover,true); // no accidental app launch',src)
        self.assertEqual(read('main/main.cpp').count('ui::BootAnimation::start()'),1)

    def test_sos_is_continuous_morse_with_correct_timing(self):
        src=read('main/apps/flashlight_signal.hpp')
        unit=int(re.search(r'kUnitMs=(\d+)',src)[1])
        units=list(map(int,re.search(r'kUnits\[\]=\{([^}]+)',src)[1].split(',')))
        self.assertEqual(units[::2],[1,1,1,3,3,3,1,1,1])
        self.assertEqual(units[1:-1:2],[1]*8)
        self.assertEqual(units[-1],7)
        self.assertEqual(sum(units)*unit,5400)
        self.assertIn('elapsedMs%kCycleMs',src)
        durations=[u*unit for u in units]
        self.assertEqual(sum(durations[::2]),sum(durations[1::2]))
        self.assertIn('static_assert(cycleUnits()==30',src)

    def test_torch_has_real_pwm_four_steps_and_one_button(self):
        src=read('main/apps/flashlight.cpp')
        self.assertEqual(src.count('compact::button('),1)
        self.assertIn('this,52,36)',src)
        self.assertIn('kLevels[]={5,30,60,100}',read('main/apps/flashlight_signal.hpp'))
        for token in ('LV_EVENT_PRESSING','std::abs(dy)>=24','!self->m_moved',
                      'Power::setTransientBrightness','Power::clearTransientBrightness',
                      'lv_timer_pause(m_timer)','EventType::ScreenOff','EventBus::unsubscribe'):
            self.assertIn(token,src)
        self.assertNotRegex(src,r'nvs_|vTaskDelay|Settings::setBrightness')
        for method in ('onHide','onDestroy'):
            self.assertIn('Flashlight::'+method,src)

    def test_settings_deep_sleep_saves_preferences_before_terminal_shutdown(self):
        src=read('main/core/power_mgr.cpp')
        proc=src.split('void Power::processStandby()',1)[1].split('void Power::syncTouch',1)[0]
        from check_native_themes import strip_theme_commit
        entry=strip_theme_commit(proc.split('if(standbyActive.load()) return;',1)[1])
        stages=['Settings::saveClock(time(nullptr))','Settings::flush(true)','displayNeedsResume=true',
                'bsp_display_suspend()','Qmi8658::suspendForStandby()','Audio::suspendForStandby()',
                'if(wakeRequested.load()) return','standbyActive.store(true)','terminalPhase.store(true)',
                'holdPerformance(false)','xTaskNotifyGive(sleepTask)','esp_wifi_stop()',
                'BleHid::prepareDeepSleep()','sleepReady.store(true)']
        offsets=[entry.index(x) for x in stages]
        self.assertEqual(offsets,sorted(offsets))
        self.assertNotRegex(proc,r'esp_(?:deep_sleep_start|light_sleep_start)|for\(;;\)')
        self.assertNotRegex(proc,r'Settings::set(?:Wifi|Bluetooth|MotionSensor)On')
        self.assertIn('acknowledgeSleepUi()',read('main/main.cpp'))
        self.assertIn('bsp_power_pause(1000)',read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c'))

    def test_settings_and_shutdown_share_internal_stack_but_pwr_only_toggles_screen(self):
        src=read('main/core/power_mgr.cpp')
        self.assertEqual(src.count('esp_deep_sleep_start()'),1)
        self.assertEqual(src.count('xTaskNotifyGive(sleepTask)'),4) # plus safe theme restart
        self.assertIn('xTaskCreateWithCaps(sleepEntry,"sleep_entry",4096,nullptr,12',src)
        self.assertIn('MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT',src)
        entry=src.split('void sleepEntry',1)[1].split('void setScreenLocked',1)[0]
        self.assertIn('if(!terminalPhase.load() && !shuttingDown.load()) continue;',entry)
        self.assertIn('pdMS_TO_TICKS(8000)',entry)
        self.assertIn('esp_sleep_enable_ext0_wakeup(kPwrPin,0)',entry)
        self.assertNotRegex(entry,r'gpio_set_level\(kBatEnPin,1\)|enable_timer_wakeup')
        off=src.split('void Power::powerOff()',1)[1]
        self.assertIn('gpio_set_level(kBatEnPin,0)',off)
        self.assertNotIn('lock(displayMutex)',off)
        self.assertIn('xTaskNotifyGive(sleepTask)',off)
        buttons=read('main/core/button_mgr.cpp')
        self.assertIn('Power::toggleScreen()',buttons)
        self.assertNotRegex(buttons,r'Power::(?:standby|deepSleep)\(')
        system=read('main/apps/settings_system.cpp')
        self.assertLess(system.index('"深度睡眠"'),system.index('"关机"'))
        self.assertIn('ServiceAction::DeepSleep',read('main/apps/settings.cpp'))

    def test_repeated_pwr_and_entry_cancellation_remain_recoverable(self):
        src=read('main/core/power_mgr.cpp')
        request=src.split('void Power::standby(bool automatic)',1)[1].split('bool Power::sleepPending',1)[0]
        self.assertIn('if(sleepRequested.load()) {\n        wakeRequested.store(true)',request)
        self.assertLess(request.index('uiStopped.store(false)'),request.index('sleepRequested.store(true)'))
        self.assertIn('wakeRequested.exchange(false)',src)
        ui=read('main/main.cpp').split('static void ui_pump_cb',1)[1].split('// Freeze diagnostics',1)[0]
        self.assertNotIn('static bool prepared',ui)
        self.assertIn('if(!Power::sleepUiReady())',ui)
        self.assertIn('WindowManager::instance().goHome()',ui)
        self.assertEqual(ui.count('Power::syncTouch()'),2)
        self.assertLess(ui.rindex('Power::syncTouch()'),ui.index('Power::acknowledgeSleepUi()'))
        wake=src.split('if(wakeRequested.exchange(false))',1)[1].split('if(standbyActive.load()) return;',1)[0]
        order=['holdPerformance(true)','bsp_display_resume()', 'Qmi8658::resumeFromStandby()',
               'Audio::resumeFromStandby()', 'sleepRequested.store(false)', 'setScreenLocked(true)']
        self.assertEqual([wake.index(x) for x in order],sorted(wake.index(x) for x in order))
        self.assertNotIn('BootAnimation::start',src)

    def test_display_suspend_resume_preserves_pwm_and_recovers_partial_failure(self):
        src=read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        block=src.split('// BEGIN BLE-preserving display standby',1)[1].split('// END BLE-preserving display standby',1)[0]
        for token in ('bsp_power_pause(1000)', 'LCD_CMD_SLPIN', 'LCD_CMD_SLPOUT',
                      'esp_lv_adapter_resume()', 'touch_needs_wake = true',
                      'tp->config.rst_gpio_num', 'esp_lcd_touch_read_data(tp)',
                      'pdMS_TO_TICKS(120)', 'pdMS_TO_TICKS(200)'):
            self.assertIn(token,block)
        self.assertNotRegex(block,r'ledc_stop|gpio_config|esp_lv_adapter_sleep_prepare|bsp_display_brightness_init')
        self.assertIn('displayNeedsResume=true;',read('main/core/power_mgr.cpp'))
        self.assertIn('press PWR to retry',read('main/core/power_mgr.cpp'))
        self.assertIn('lv_obj_invalidate(lv_screen_active())',read('main/core/power_mgr.cpp'))

    def test_imu_and_audio_resume_restore_state_without_persisting(self):
        imu=read('main/core/qmi8658.cpp').split('bool Qmi8658::suspendForStandby()',1)[1]
        for token in ('s_restoreEnabled=s_enabled.load()', 'ctrl1|0x02', 'ctrl1 & ~0x02',
                      'writeRegister(kCtrl7,s_restoreEnabled?kAccelEnable:0x00)', 's_suspended=false'):
            self.assertIn(token,imu)
        audio=read('main/core/audio_mgr.cpp').split('void Audio::suspendForStandby()',1)[1].split('bool Audio::openStream',1)[0]
        # Streams are now destroyed, not kept muted while asleep. Active
        # streams remain owned; resume stays lazy and next open restores volume.
        for token in ('if(s_suspended || s_stream) return', 'releaseAudioLocked()',
                      's_suspended=true', 's_suspended=false'):
            self.assertIn(token,audio)
        self.assertNotRegex(audio,r'Settings::set|esp_codec_dev_open|\binit\(')
        full=read('main/core/audio_mgr.cpp')
        self.assertIn('const auto err=bsp_audio_deinit()',full)
        self.assertIn('esp_codec_dev_set_out_vol(s_stream,Settings::volume())',full)

    def test_pm_uses_main_xtal_modem_sleep_and_gated_automatic_light_sleep(self):
        config=read('sdkconfig')
        for token in ('CONFIG_PM_ENABLE=y', 'CONFIG_BT_CTRL_MODEM_SLEEP=y',
                      'CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1=y', 'CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y',
                      'CONFIG_BT_CTRL_SLEEP_MODE_EFF=1', 'CONFIG_BT_CTRL_SLEEP_CLOCK_EFF=1'):
            self.assertIn(token,config)
        self.assertNotRegex(config,re.compile(r'^CONFIG_(?:BT_CTRL_LPCLK_SEL_RTC_SLOW|RTC_CLK_SRC_EXT_CRYS)=y',re.M))
        power=read('main/core/power_mgr.cpp')
        for token in ('config.min_freq_mhz=40', 'config.light_sleep_enable=true',
                      'ESP_PM_CPU_FREQ_MAX', 'esp_pm_lock_acquire', 'esp_pm_lock_release'):
            self.assertIn(token,power)
        self.assertIn('esp_pm',read('main/CMakeLists.txt'))

    def test_boot_wake_clock_and_key_are_not_replayed(self):
        self.assertIn('if(!Power::wokeFromSleep() || time(nullptr)<1609459200LL)',read('main/main.cpp'))
        src=read('main/core/button_mgr.cpp').split('void Buttons::init',1)[1]
        self.assertLess(src.index('xTaskCreate(pwrWatchdogTask'),src.index('if(Power::wokeFromSleep())'))
        self.assertLess(src.index('while(gpio_get_level(kBtnPwr)==0)'),src.index('createButton(kBtnPwr'))
        self.assertIn('key.mode=GPIO_MODE_INPUT',read('main/core/power_mgr.cpp'))

    def test_normal_screen_off_keeps_radios_and_deep_terminal_stops_host_access(self):
        src=read('main/core/ble_hid.cpp')
        code=re.sub(r'//[^\n]*','',src.split('esp_err_t BleHid::setEnabled',1)[1].split('esp_err_t BleHid::disconnect',1)[0])
        self.assertNotRegex(code,r'\b(?:nimble_port_stop|nimble_port_deinit|esp_hidd_dev_deinit|esp_bt_controller_disable)\s*\(')
        deep=src.split('esp_err_t BleHid::prepareDeepSleep()',1)[1]
        self.assertIn('nimble_port_stop()',deep)
        self.assertNotRegex(re.sub(r'//[^\n]*','',deep),r'(?:esp_hidd_dev_deinit|nimble_port_deinit)\s*\(')
        service=read('main/core/system_service.cpp')
        for token in ('Connectivity::poll()', 'Power::processStandby()', 'refreshBluetooth()',
                      'Power::sleepPending()?ESP_ERR_INVALID_STATE:execute(cmd)',
                      'if(Power::sleepPending()) return;'):
            self.assertIn(token,service)
        worker=service.split('void worker(void *)',1)[1]
        self.assertLess(worker.index('if(Power::sleepTerminal())'),worker.index('Connectivity::poll()'))
        self.assertLess(worker.index('Power::processStandby()'),worker.rindex('if(Power::sleepTerminal())'))
        power=read('main/core/power_mgr.cpp')
        toggle=power.split('void Power::toggleScreen()',1)[1].split('bool Power::isScreenOn',1)[0]
        self.assertIn('setScreenLocked(!screen.load())',toggle)
        self.assertNotRegex(toggle,r'BleHid|Connectivity|esp_wifi|standby\(')
        help_text=read('main/apps/settings_system.cpp')
        self.assertIn('PWR 单击亮灭屏',help_text)
        self.assertNotIn('PWR 单击待机',help_text)

    def test_new_lvgl_calls_exist_in_local_headers(self):
        headers='\n'.join(p.read_text(encoding='utf-8',errors='replace') for p in (ROOT/'../dependencies/managed_components/lvgl__lvgl').rglob('*.h'))
        for path in ('main/ui/text_input.cpp','main/ui/boot_animation.cpp','main/apps/flashlight.cpp'):
            for call in set(re.findall(r'\b(lv_\w+)\s*\(',read(path))):
                self.assertRegex(headers,rf'\b{call}\b',call)

if __name__=='__main__': unittest.main(verbosity=2)
