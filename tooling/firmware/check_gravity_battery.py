"""Gravity prompt source contracts and battery-log/Python specification checks.
No C/C++ execution, build, configure, flash, reboot or serial access.
"""
from pathlib import Path
import json,re,unittest
from check_battery_power import body
import importlib.util
# Historic logs must be checked against the pre-rescale estimator, not relabelled
# as new-firmware measurements. Load the frozen Python model and its old header.
_history_path=(Path(__file__).resolve().parents[2]/'firmware')/'../tooling/firmware/fixtures/contracts/battery-full91-20260917-172558/tools/check_battery_power.py'
_history_spec=importlib.util.spec_from_file_location('battery_before_full91',_history_path)
_history=importlib.util.module_from_spec(_history_spec);_history_spec.loader.exec_module(_history)
Model,feed,percent_for_voltage,K=_history.Model,_history.feed,_history.percent_for_voltage,_history.K
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/gravity-battery-20260917-170449'
BASE=ROOT/'../tooling/firmware/fixtures/contracts/gravity-battery-20260917-170449'
def read(name,base=ROOT):return (base/name).read_text(encoding='utf-8-sig')
def part(text,a,b):return text.split(a,1)[1].split(b,1)[0]
def capture():return json.loads((RUN/'battery_capture.status.json').read_text(encoding='utf-8'))

class GravityPromptContracts(unittest.TestCase):
    def test_ready_playfield_has_no_app_text_or_buttons(self):
        # The former free-moving gravity demo was explicitly removed.
        for ext in ('cpp','hpp'):self.assertFalse((ROOT/f'main/apps/gravity_ball.{ext}').exists())
        s=read('main/apps/gravity_maze.cpp')
        play=body(s,'void GravityMaze::play(')
        self.assertIn('lv_obj_set_hidden(menu,true)',play)
        board=body(s,'void GravityMaze::buildBoard(')
        self.assertNotIn('c::button(boardView',board)
        self.assertNotIn('c::label(boardView',board)
    def test_off_busy_and_failed_prompts_remain_and_use_chinese_font(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertIn('重力感应不可用',s)
        self.assertIn('点击继续重试',s)
        label=part(read('main/ui/compact_ui.cpp'),'lv_obj_t *label(', 'lv_obj_t *button(')
        self.assertIn('lv_obj_set_style_text_font(obj,theme::fontSmall(),0)',label)
        self.assertIn('sample.timeMs)<250',s)
        self.assertIn('std::isfinite(sample.value.x)',s)
    def test_enable_button_keeps_action_size_and_position(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertIn('button("开始",Action::Play,20,158)',s)
        self.assertIn('game_button(menu,text,x,y,96,36',s)
        self.assertIn('Qmi8658::claimTemporaryUse()',body(s,'void GravityMaze::play('))
        self.assertNotIn('ServiceAction::Motion',s)
    def test_prompt_refreshed_on_create_show_and_awake_tick(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertIn('ScreenOff',s)
        self.assertIn('pause("已暂停',s)
        tick=body(s,'void GravityMaze::tick(')
        self.assertLess(tick.index('Power::isScreenOn'),tick.index('temporaryAcceleration'))
        self.assertIn('now-started)>2000',tick)
        self.assertIn('lastSample)>=250',tick)
    def test_simulation_and_teardown_unchanged(self):
        # Replacement has bounded physics and releases its temporary sensor demand.
        s=read('main/apps/gravity_maze.cpp')
        self.assertIn('releaseTemporaryUse(lease)',s)
        hidden=body(s,'void GravityMaze::onHide(')
        for token in ('releaseSensor()', 'releasePageKeys(keySession)', 'lv_timer_pause(timer)'):self.assertIn(token,hidden)
        destroy=body(s,'void GravityMaze::onDestroy(')
        for token in ('onHide()', 'EventBus::unsubscribe(screenSub)', 'lv_timer_delete(timer)'):self.assertIn(token,destroy)
        self.assertIn('steps>3',s)
        self.assertNotIn('readAcceleration(',s)
    def test_other_main_edits_only_update_source_revision(self):
        for name in ('main/main.cpp','main/apps/settings_system.cpp'):
            old=read(name,BASE)
            self.assertEqual(read(name),old.replace('NETDISK-CLEANUP-20260917','BATTERY-CLEAN-20260917'))

class BatteryCaptureContracts(unittest.TestCase):
    def test_capture_is_passive_closed_and_fresh_samples_are_monotonic(self):
        c=capture()
        self.assertEqual(c['status'],'succeeded');self.assertTrue(c['serial_released'])
        self.assertFalse(c['reset_requested']);self.assertFalse(c['serial_writes'])
        self.assertFalse(c['unexpected_boot_marker']);self.assertEqual(c['error_lines'],[])
        self.assertGreaterEqual(c['seconds'],125)
        text=(RUN/'battery_live.samples.txt').read_bytes().decode('utf-8',errors='replace')
        pattern=r'I \((\d+)\) battery: estimate=(\d+)% voltage=(\d+)mV charge=(\w+) adc=6dB valid_samples=(\d+)'
        parsed=[(int(t),int(p),int(v),q,int(n)) for t,p,v,q,n in re.findall(pattern,text)]
        expected=[(s['device_uptime_ms'],s['percent'],s['millivolts'],s['charge'],s['valid_samples']) for s in c['samples']]
        self.assertEqual(parsed,expected);self.assertEqual(len(parsed),4)
        times=[p[0] for p in parsed]
        self.assertEqual(times,sorted(set(times)))
        self.assertGreater(times[0],1600000) # excludes the initial stale partial line
        self.assertTrue(all(30000<=b-a<=32000 for a,b in zip(times,times[1:])))
    def test_observed_voltage_matches_percent_but_not_full_threshold(self):
        for sample in capture()['samples']:
            self.assertEqual(sample['valid_samples'],16)
            self.assertEqual(sample['charge'],'inactive')
            self.assertEqual(percent_for_voltage(sample['millivolts']),sample['percent'])
            self.assertEqual(sample['percent'],92)
            self.assertLess(sample['millivolts'],K['kFullMillivolts'])
            self.assertLess(sample['millivolts'],K['kBootFullMillivolts'])
    def test_reference_model_does_not_force_this_plateau_to_full(self):
        # Counterfactual fixed-voltage runs, not replay of unlogged 1s CHG or
        # unfiltered ADC data and not evidence of two hours of real charging.
        voltages=sorted({s['millivolts'] for s in capture()['samples']})
        for mv in voltages:
            for period in (5000,15000):
                for charge in (False,True):
                    m=Model();samples=feed(m,0,7200000,mv,charge,period)
                    self.assertTrue(all(s['percent']==92 for _,s in samples),(mv,period,charge))
    def test_reference_model_can_converge_from_91_to_92(self):
        for period in (5000,15000):
            m=Model();feed(m,0,60000,4110,False,period)
            self.assertEqual(m.snapshot(60000)['percent'],91)
            feed(m,61000,660000,4116,False,period)
            self.assertEqual(m.snapshot(660000)['percent'],92)
    def test_battery_acquisition_and_other_retained_features_preserved(self):
        from check_battery_diagnostics_cleanup import verify_sampling_preserved
        verify_sampling_preserved(self)
        # Battery acquisition API remains; external-power classification adds
        # separate accessors without replacing voltage/charge measurements.
        current=read('main/core/battery_mgr.hpp')
        prior=read('main/core/battery_mgr.hpp',BASE)
        for declaration in ('static bool init();','static bool available();','static void poll(bool screenOn);',
                            'static void sample();','static BatterySnapshot snapshot();',
                            'static float voltage();','static int level();'):
            self.assertIn(declaration,prior);self.assertIn(declaration,current)
        # The overlay is now explicitly being improved. Keep its power contract
        # instead of freezing an obsolete and incorrectly labelled memory view.
        overlay=read('main/ui/sysinfo_overlay.cpp')
        for token in ('s_visible && sys::Power::isScreenOn()', 'if(!active) return',
                      'lv_timer_pause(s_timer)', 'setInstrumentationActive(active)'):
            self.assertIn(token,overlay)

if __name__=='__main__':unittest.main(verbosity=2)
