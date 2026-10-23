"""Removal of periodic battery diagnostics, without changing battery behavior."""
from pathlib import Path
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BASE=ROOT/'../tooling/firmware/fixtures/contracts/battery-clean-buildflash-20260917-173715'
BLOCK='''    if(now>=s_nextDiagnostic) {
        s_nextDiagnostic=now+30000;const auto battery=s_estimator.snapshot(now);
        ESP_LOGI("battery","estimate=%d%% voltage=%dmV charge=%s adc=6dB valid_samples=%d",
            battery.percent,battery.millivolts,!battery.chargingKnown?"unknown":battery.charging?"active":"inactive",count);
    }
'''
def read(name,base=ROOT):return (base/name).read_text(encoding='utf-8-sig')
def verify_sampling_preserved(test):
    old=read('main/core/battery_mgr.cpp',BASE)
    test.assertEqual(old.count(BLOCK),1)
    expected=old.replace('int64_t s_nextSample=0,s_nextDiagnostic=0;','int64_t s_nextSample=0;').replace(BLOCK,'')
    current=read('main/core/battery_mgr.cpp')
    # Freeze battery measurement independently from the new supply detector.
    for start,end in (('void Battery::sample()', 'void Battery::poll'),
                      ('BatterySnapshot Battery::snapshot()', 'bool Battery::available()')):
        test.assertEqual(current.split(start,1)[1].split(end,1)[0],
                         expected.split(start,1)[1].split(end,1)[0])
    start='    adc_oneshot_unit_init_cfg_t initCfg{};'
    test.assertEqual(current.split(start,1)[1].split('    s_ready.store(true);',1)[0],
                     expected.split(start,1)[1].split('    gpio_config_t charge{};',1)[0])
    tail='    if(s_chargeReady) s_estimator.observeCharge'
    test.assertEqual(current.split(tail,1)[1].split('BatterySnapshot Battery::snapshot',1)[0],
                     expected.split(tail,1)[1].split('BatterySnapshot Battery::snapshot',1)[0])
class BatteryDiagnosticsCleanup(unittest.TestCase):
    def test_only_periodic_log_and_deadline_removed(self):verify_sampling_preserved(self)
    def test_no_diagnostic_strings_or_state_remain_in_firmware_sources(self):
        for p in (ROOT/'main').rglob('*'):
            if p.suffix not in ('.cpp','.hpp','.c','.h'):continue
            text=p.read_text(encoding='utf-8-sig')
            for token in ('s_nextDiagnostic','estimate=%d%% voltage=%dmV charge=%s adc=6dB valid_samples=%d'):
                self.assertNotIn(token,text,str(p))
    def test_real_error_reporting_and_sampling_publish_remain(self):
        s=read('main/core/battery_mgr.cpp')
        self.assertIn('ADC calibration unavailable; show unknown, not raw SOC',s)
        self.assertIn('CHG input unavailable; charge state unknown',s)
        self.assertIn('publish(now);',s);self.assertIn('s_estimator.observeVoltage',s)
        self.assertIn('kSamples=16, kMinimumSamples=12',s)
        self.assertIn('gpio_get_level(kChargePin)==0',s)
        self.assertNotIn('gpio_set_level',s)
    def test_new_curve_gravity_and_other_retained_features_frozen(self):
        for name in ('main/core/battery_estimator.hpp','main/apps/gravity_ball.cpp','main/apps/gravity_ball.hpp',
                     'main/ui/sysinfo_overlay.cpp','main/apps/touch_scope.cpp','main/apps/touch_scope.hpp',
                     'main/core/ble_hid.cpp','main/core/power_mgr.cpp','main/ui/window_mgr.cpp','main/apps/settings.cpp'):
            self.assertEqual((ROOT/name).read_bytes(),(BASE/name).read_bytes(),name)
    def test_other_main_changes_are_only_revision(self):
        for name in ('main/main.cpp','main/apps/settings_system.cpp'):
            self.assertEqual(read(name),read(name,BASE).replace('BATTERY-FULL91-20260917','BATTERY-CLEAN-20260917'))
if __name__=='__main__':unittest.main(verbosity=2)
