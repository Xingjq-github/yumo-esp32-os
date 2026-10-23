"""SDK boundary/lifecycle contracts; arithmetic and adapter behavior have native replays."""
from pathlib import Path
import os, unittest
ROOT=Path(__file__).resolve().parents[2]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def read_sdk(path):
    sdk=os.environ.get('IDF_PATH')
    if not sdk:raise RuntimeError('Activate the pinned ESP-IDF v6.1 environment (IDF_PATH) before SDK contract checks')
    return (Path(sdk)/path).read_text(encoding='utf-8')
class ClockDriftContracts(unittest.TestCase):
    def test_real_sleep_calibration_boundary(self):
        sdk=read_sdk('components/esp_hw_support/sleep_modes.c')
        self.assertIn('sleep_low_power_clock_calibration(true);',sdk)
        self.assertIn('rtc_clk_cal(CLK_CAL_RTC_SLOW, RTC_CLK_SRC_CAL_CYCLES)',sdk)
        cmake=read('firmware/main/CMakeLists.txt')
        self.assertIn('--wrap=rtc_clk_cal',cmake)
        power=read('firmware/main/core/power_mgr.cpp')
        self.assertIn('ClockDiscipline::prepareSleep();\n        esp_deep_sleep_start();',power)
    def test_sntp_capture_preserves_sdk_completion(self):
        sdk=read_sdk('components/lwip/apps/sntp/sntp.c')
        self.assertIn('void sntp_set_system_time(uint32_t sec, uint32_t us)',sdk)
        adapter=read('firmware/main/core/clock_discipline.cpp')
        wrapper=adapter[adapter.index('void __wrap_sntp_set_system_time'):adapter.index('namespace sys {')]
        self.assertLess(wrapper.index('before=wallUs()'),wrapper.index('__real_sntp_set_system_time(sec,us)'))
        self.assertNotIn('Settings::',wrapper)
        service=read('firmware/main/core/system_service.cpp')
        stop=service[service.index('void stopTimeClient()'):service.index('void finishTime(')]
        self.assertLess(stop.index('esp_netif_sntp_deinit()'),stop.index('ClockDiscipline::networkStopped()'))
    def test_model_is_settings_owned_and_boot_precedes_ui(self):
        settings=read('firmware/main/core/settings_store.cpp')
        self.assertIn('nvs_set_blob(h,"rtc_drift_v1"',settings)
        adapter=read('firmware/main/core/clock_discipline.cpp')
        self.assertNotIn('nvs_',adapter)
        main=read('firmware/main/main.cpp')
        self.assertLess(main.index('Settings::load();'),main.index('ClockDiscipline::boot();'))
        self.assertLess(main.index('ClockDiscipline::boot();'),main.index('// Build UI while'))
    def test_manual_time_invalidates_only_observation(self):
        service=read('firmware/main/core/system_service.cpp')
        manual=service[service.index('esp_err_t applyClock('):service.index('esp_err_t execute(')]
        self.assertLess(manual.index('settimeofday'),manual.index('ClockDiscipline::manualTimeChanged()'))
        self.assertIn('manualTimeChanged() {retained.window={};seal();}',read('firmware/main/core/clock_discipline.cpp'))
if __name__=='__main__':unittest.main()
