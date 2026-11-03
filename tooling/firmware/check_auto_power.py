"""Automatic power policy integration checks; runtime timing is asserted in C++."""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def assert_config(test,name):
    expected=json.loads(read('../tooling/firmware/fixtures/contracts/current-firmware-config.json'))
    test.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),expected[name],name)
    s=read(name)
    for option in ('CONFIG_FREERTOS_USE_TICKLESS_IDLE=y','CONFIG_PM_ENABLE=y',
                   'CONFIG_BT_NIMBLE_MAX_CCCDS=128','CONFIG_BT_NIMBLE_MAX_BONDS=16',
                   'CONFIG_BT_NIMBLE_NVS_PERSIST=y',
                   'CONFIG_COMPILER_CXX_EXCEPTIONS_EMG_POOL_SIZE=1024'):
        test.assertIn(option,s)
    if name=='sdkconfig':
        for option in ('CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y',
                       'CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y','CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y'):
            test.assertIn(option,s)
        test.assertNotRegex(s,r'(?m)^CONFIG_PM_(?:POWER_DOWN_CPU_IN_LIGHT_SLEEP|SLP_SPIRAM_HALFSLEEP_ENABLED)=y$')
def original_sos(text):
    # Only the new deadline helper is removed; levels, timing and pattern remain frozen.
    return re.sub(r'constexpr uint32_t untilEdge\(uint32_t elapsedMs\) \{.*?\n\}\n','',text,flags=re.S)
class AutoPowerContracts(unittest.TestCase):
    def test_supply_freshness_is_independent_of_service_io(self):
        runtime=read('main/core/power_runtime.cpp').split('void Power::pollAutomatic',1)[1]
        self.assertLess(runtime.index('Battery::pollSupply()'),runtime.index('activitySerial.load()'))
        battery=read('main/core/battery_mgr.cpp')
        supply=battery.split('void Battery::pollSupply',1)[1].split('void Battery::poll(bool',1)[0]
        self.assertIn('lock(s_supplyMutex)',supply)
        self.assertIn('s_nextSupply=now+1000',supply)
        self.assertNotRegex(supply,r'adc_\w+\(|sample\(|vTaskDelay|i2c_\w+\(')
        self.assertIn('pollSupply(true)',battery.split('void Battery::poll(bool',1)[1])

    def test_dark_touch_cannot_extend_idle_or_wake_light_sleep(self):
        port=read('components/esp32_s3_touch_lcd_1_54/lvgl_power_adapter.c')
        irq=port.split('void bsp_power_touch_irq',1)[1].split('void bsp_power_touch_wakeup',1)[0]
        self.assertNotIn('activity_cb',irq)
        self.assertIn('touch_active',irq)
        sync=read('main/core/power_mgr.cpp').split('void Power::syncTouch()',1)[1].split('void Power::powerOff',1)[0]
        self.assertIn('gpio_wakeup_enable(BSP_LCD_TOUCH_INT',sync)
        self.assertIn('gpio_wakeup_disable(BSP_LCD_TOUCH_INT)',sync)
        self.assertIn('bsp_power_touch_active(desired)',sync)
        self.assertNotIn('kPwrPin',sync)
        touch=read('components/esp32_s3_touch_lcd_1_54/lvgl_power_touch.c')
        self.assertIn('touch_read_result!=ESP_OK',touch)
        self.assertIn('bsp_power_touch_sample(*count>0)',touch)

    def test_actual_input_and_worker_only_radio_changes(self):
        s=read('main/core/power_runtime.cpp')
        gui=s.split('void Power::pollAutomatic',1)[1].split('void Power::processEconomy',1)[0]
        self.assertNotIn('setWifiEnabled',gui);self.assertNotIn('setEnabled',gui)
        for call in ('!vault::idle()','!core::recorder::idle()','!core::fc::idle()',
                     '!Screenshot::idle()','!LanHotspot::idle()','!BleHid::privateInputIdle()'):
            self.assertIn(call,s)
        self.assertIn('if(d.deep && !isScreenOn() && automaticDeepSleepAllowed()) (void)deepSleep(true)',s)
        self.assertNotIn('lv_display_get_inactive_time',s)
        # Lit sessions keep their protection; dark deadlines apply to all sessions.
        idle=re.search(r'const bool idle=([^;]+);',gui).group(1)
        self.assertIn('!(hints&power::KeepSession)',idle)
        self.assertIn('d.stage==power::Stage::Standby ||',idle)
    def test_light_sleep_hardware_and_deep_wake_are_separate(self):
        s=read('main/core/power_mgr.cpp')
        self.assertLess(s.index('esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)'),s.index('esp_sleep_enable_ext0_wakeup'))
        self.assertIn('ESP_PM_NO_LIGHT_SLEEP',s)
        bsp=read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
        for token in ('LEDC_SLEEP_MODE_KEEP_ALIVE','LEDC_USE_RC_FAST_CLK','gpio_wakeup_enable',
                      'esp_sleep_get_wakeup_causes','power_touch_original_irq(handle)'):
            self.assertIn(token,bsp)
        wake=bsp.split('static esp_err_t power_touch_after_sleep',1)[1].split('static lv_indev_t',1)[0]
        self.assertIn('elapsed>0',wake)
        self.assertIn('ESP_SLEEP_WAKEUP_GPIO',wake)
        self.assertIn('bsp_power_touch_wakeup()',wake)
        self.assertNotIn('esp_sleep_get_gpio_wakeup_status',wake)
        port=read('components/esp32_s3_touch_lcd_1_54/lvgl_power_adapter.c')
        for token in ('lv_tick_set_cb(monotonic_ms)','ulTaskNotifyTake','vTaskNotifyGiveFromISR','lv_timer_ready(pump_timer)'):
            self.assertIn(token,port)
        self.assertIn('if(xTaskGetCurrentTaskHandle()!=s_ctx.task) {vTaskDelay(ticks);return;}',port)
    def test_recording_keeps_running_only_for_automatic_blackout(self):
        s=read('main/core/power_mgr.cpp')
        self.assertIn('(screen.load() || automaticDark.load()) && !sleepPending()',s)
        recorder=read('main/core/audio_recorder.cpp')
        playback,recording=recorder.split('void execute(Job &job)',1)
        self.assertEqual(playback.count('!sys::Power::audioMayRun()'),2)
        self.assertEqual(recording.count('!sys::Power::audioMayRun()'),2)
        self.assertIn('automaticDark=false;setScreenLocked(false)',s)
    def test_display_controls_are_ordered_by_kind(self):
        s=read('main/apps/settings_system.cpp').split('void Settings::buildDisplay()',1)[1].split('void Settings::sliderCb',1)[0]
        self.assertLess(s.index('v.quietBrightness=slider'),s.index('v.castToggle=c::toggle'))
        self.assertLess(s.index('v.castToggle=c::toggle'),s.index('v.screenTime=powerTime'))
        self.assertLess(s.index('v.quietTime=powerTime'),s.index('v.castStatus=c::label'))
    def test_full_cjk_font_covers_new_setting_labels(self):
        # GB2312 font already covers these UI characters; no new bitmap payload.
        ui=read('main/apps/settings_system.cpp')
        labels=re.findall(r'"([^"\n]*)"',ui)
        for char in set(''.join(labels)):
            if '\u4e00'<=char<='\u9fff':self.assertTrue(char.encode('gb2312'),char)
    def test_supply_checked_before_sleep_teardown_and_economy(self):
        worker=read('main/core/system_service.cpp').split('const auto supplyNow',1)[1]
        self.assertLess(worker.index('Battery::poll'),worker.index('Power::processEconomy'))
        s=read('main/core/power_mgr.cpp').split('void Power::processStandby()',1)[1]
        self.assertLess(s.index('Battery::poll'),s.index('if(wakeRequested.exchange(false))'))
        tail=s.split('Audio::suspendForStandby();',1)[1]
        self.assertLess(tail.index('!automaticDeepSleepAllowed()'),tail.index('terminalPhase.store(true)'))
        self.assertIn('automaticSleepRequested.load()',tail)
        runtime=read('main/core/power_runtime.cpp')
        self.assertIn('(void)deepSleep(true)',runtime)
        self.assertIn('if(!automaticScreenOffAllowed())restoreAutomaticScreen()',runtime)
        self.assertIn('return Settings::powerConfig().sleepOnPower || !Battery::supplyProtected()',runtime)
        self.assertIn('!(c.protectLinks && linkConnected())',runtime)
        radio=read('main/core/connectivity.cpp')
        self.assertIn('Power::automaticSleepAllowed()?WIFI_PS_MIN_MODEM:WIFI_PS_NONE',radio)
    def test_dark_deadlines_and_usb_fence(self):
        policy=read('main/core/power_policy.hpp').split('out.stage=Stage::Dark;',1)[1]
        self.assertNotRegex(policy,r'in\.(?:sleepBusy|supplyProtected|connected|hints|activity)')
        self.assertIn('in.now-darkAt',policy)
        runtime=read('main/core/power_runtime.cpp')
        deep=runtime.split('if(d.deep',1)[1].split('\n',1)[0]
        self.assertNotIn('jobsBusy',deep)
        mgr=read('main/core/power_mgr.cpp')
        request=mgr.split('void Power::standby(bool automatic)',1)[1].split('bool Power::sleepPending',1)[0]
        self.assertIn('if(automatic) (void)UsbStorage::setEnabled(false)',request)
        drain=mgr.split('void Power::processStandby()',1)[1]
        self.assertLess(drain.index('if(!UsbStorage::idle())return'),drain.index('Settings::saveClock'))
    def test_supply_does_not_infer_vbus_from_battery_percent(self):
        s=read('main/core/battery_mgr.cpp').split('void Battery::poll',1)[1].split('BatterySnapshot Battery::snapshot',1)[0]
        self.assertIn('usb_serial_jtag_is_connected()',s)
        self.assertIn('gpio_get_level(kChargePin)==0',s)
        code=re.sub(r'//[^\n]*','',s)
        self.assertNotIn('percent',code);self.assertNotIn('millivolts',code)
        persist=read('main/core/settings_store.cpp')
        self.assertIn('nvs_get_blob(h,"power_v1",legacy.data()',persist)
        self.assertIn('nvs_set_blob(h,"power_v2",energy.data()',persist)
        self.assertNotIn('nvs_erase',persist)
