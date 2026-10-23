"""User-selected full-at-old-91 display calibration: source and Python models.
Does not compile C++, change charger settings, or communicate with hardware.
"""
from pathlib import Path
import re,unittest
from check_battery_power import Model,feed,percent_for_voltage,K,CURVE,body
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BASE=ROOT/'../tooling/firmware/fixtures/contracts/battery-full91-20260917-172558'
def read(name,base=ROOT):return (base/name).read_text(encoding='utf-8-sig')

class Full91CalibrationContracts(unittest.TestCase):
    def test_curve_normalizes_old_91_and_preserves_empty_endpoint(self):
        # Keep the user's full endpoint, not the superseded global 100/91 gain.
        self.assertEqual(CURVE[0],(3300,0));self.assertEqual(CURVE[-1],(4110,100))
        self.assertTrue(35<=percent_for_voltage(3800)<=45)
        self.assertLessEqual(percent_for_voltage(3800)-percent_for_voltage(3700),24)
        self.assertTrue(all(a[0]<b[0] and a[1]<b[1] for a,b in zip(CURVE,CURVE[1:])))
        self.assertEqual(K['kFullMillivolts'],4110)
        for mv in range(2500,3301):self.assertEqual(percent_for_voltage(mv),0)
        for mv in range(4110,4501):self.assertEqual(percent_for_voltage(mv),100)
    def test_cold_and_post_charge_endpoint_qualifies_without_old_4160_gate(self):
        for mv in (4110,4115,4116,4180,4200):
            for period in (5000,15000):
                cold=Model();initial=feed(cold,0,120000,mv,False,period)
                self.assertTrue(all(s['percent']==99 for _,s in initial))
                feed(cold,121000,600000,mv,False,period)
                self.assertEqual(cold.percent,100,(mv,period,'cold'))
                charged=Model();feed(charged,0,300000,mv,True,period)
                self.assertEqual(charged.percent,99)
                feed(charged,301000,420000,mv,False,period)
                self.assertEqual(charged.percent,99)
                feed(charged,421000,900000,mv,False,period)
                self.assertEqual(charged.percent,100,(mv,period,'after charge'))
    def test_below_endpoint_never_rounds_its_way_to_full(self):
        for mv in (4000,4090,4100,4104,4105,4109):
            for period in (5000,15000):
                m=Model();feed(m,0,1200000,mv,False,period)
                self.assertEqual(m.percent,min(percent_for_voltage(mv),99),(mv,period))
                self.assertLess(m.percent,100)
    def test_active_unknown_and_unstable_signals_cannot_qualify_full(self):
        for period in (5000,15000):
            active=Model();samples=feed(active,0,1200000,4116,True,period)
            self.assertTrue(all(s['percent']==99 for _,s in samples))
            unknown=Model()
            for t in range(0,1200001,period):
                unknown.voltage(4116,t);self.assertEqual(unknown.percent,99)
                self.assertFalse(unknown.snapshot(t)['known'])
            unstable=Model();samples=feed(unstable,0,1200000,lambda t:4050 if (t//30000)%2==0 else 4130,False,period)
            self.assertTrue(all(s['percent']<100 for _,s in samples))
    def test_full_still_falls_and_reinsertion_keeps_real_charging_state(self):
        for period in (5000,15000):
            m=Model();feed(m,0,600000,4116,False,period);self.assertEqual(m.percent,100)
            feed(m,601000,660000,4116,True,period)
            self.assertEqual(m.percent,99);self.assertTrue(m.snapshot(660000)['charging'])
            samples=feed(m,661000,1500000,3300,False,period)
            self.assertEqual(m.percent,0)
            self.assertTrue(all(a[1]['percent']>=b[1]['percent'] for a,b in zip(samples,samples[1:])))
    def test_stale_sample_recovery_requalifies_the_new_endpoint(self):
        for period in (5000,15000):
            m=Model();feed(m,0,300000,4116,False,period);self.assertEqual(m.percent,100)
            self.assertFalse(m.snapshot(360001)['valid'])
            m.charge(False,370000);m.voltage(4116,370000)
            self.assertEqual(m.percent,99);self.assertFalse(m.snapshot(370000)['known'])
            feed(m,371000,490000,4116,False,period);self.assertEqual(m.percent,99)
            feed(m,491000,900000,4116,False,period);self.assertEqual(m.percent,100)
    def test_no_old_gate_or_charge_history_and_no_hardware_override(self):
        s=read('main/core/battery_estimator.hpp');old=read('main/core/battery_estimator.hpp',BASE)
        for removed in ('kBootFullMillivolts','chargedHigh','fullThreshold','4160','4180'):
            self.assertNotIn(removed,s)
        self.assertIn('known && !charging && voltage>=kFullMillivolts',s)
        self.assertIn('now-fullAt>=kFullHoldMs',s)
        self.assertIn('kCurve[sizeof(kCurve)/sizeof(kCurve[0])-1].millivolts==kFullMillivolts',s)
        self.assertEqual(body(s,'constexpr void observeCharge('),body(old,'constexpr void observeCharge('))
        self.assertEqual(body(s,'constexpr BatterySnapshot snapshot('),body(old,'constexpr BatterySnapshot snapshot('))
        from check_battery_diagnostics_cleanup import verify_sampling_preserved
        verify_sampling_preserved(self)
        for name in ('main/core/battery_mgr.hpp','main/core/power_mgr.cpp',
                     'main/core/system_service.cpp','main/ui/window_mgr.cpp','main/apps/settings.cpp',
                     'main/core/ble_hid.cpp','main/apps/gravity_ball.cpp','main/apps/gravity_ball.hpp'):
            self.assertEqual((ROOT/name).read_bytes(),(BASE/name).read_bytes(),name)
    def test_source_revision_only_and_cpp_spec_updated_without_execution(self):
        for name in ('main/main.cpp','main/apps/settings_system.cpp'):
            self.assertEqual(read(name),read(name,BASE).replace('GRAVITY-UI-20260917','BATTERY-CLEAN-20260917'))
        s=read('../tooling/firmware/keyboard_battery_model_test.cpp')
        self.assertIn('static_assert(batterySmallChange()',s)
        self.assertIn('feed(cold,0,600000,4110,false)',s)
        self.assertIn('feed(below,0,600000,4109,false)',s)

if __name__=='__main__':unittest.main(verbosity=2)
