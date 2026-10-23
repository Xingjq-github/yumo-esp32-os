"""Battery/power source contracts and independent Python behavior models.
NO C/C++ execution, compiler, configure, build, flash or serial/device access.
The model reads constants from the C++ header but is not a firmware runtime test.
"""
from pathlib import Path
import hashlib
import json
import random
import re
import struct
import unittest
from check_firmware_refactor import code_only

ROOT=Path(__file__).resolve().parents[1]
RUN=ROOT/'.joycode/runs/battery-power-20260916'
BACKUP=ROOT/'.joycode/backups/battery-power-20260916'
SDK=ROOT.parent/'esp-env/esp-idf'
REFERENCE=ROOT.parent/'ESP32-S3-Touch-LCD-1.54/examples/ESP32-S3-Touch-LCD-1.54-demo'
def read(name): return (ROOT/name).read_text(encoding='utf-8-sig')
def part(text,start,end): return text.split(start,1)[1].split(end,1)[0]
def body(text,signature):
    s=code_only(text).split(signature,1)[1]; start=s.index('{'); depth=0
    for i in range(start,len(s)):
        if s[i]=='{': depth+=1
        if s[i]=='}':
            depth-=1
            if not depth: return s[start:i+1]
    raise AssertionError('unclosed scope')
ESTIMATOR=read('main/core/battery_estimator.hpp')
K={name:int(value) for name,value in re.findall(r'\b(k\w+)=(\d+)',ESTIMATOR)}
CURVE=[tuple(map(int,p)) for p in re.findall(r'\{(\d+),(\d+)\}',part(ESTIMATOR,'kCurve[]=',';'))]
def f32(x): return struct.unpack('f',struct.pack('f',x))[0]
def clamp(x,lo,hi): return max(lo,min(hi,x))
def percent_for_voltage(mv):
    if mv<=CURVE[0][0]: return 0
    for (v0,p0),(v1,p1) in zip(CURVE,CURVE[1:]):
        if mv<=v1: return p0+((mv-v0)*(p1-p0)+(v1-v0)//2)//(v1-v0)
    return 100

def trimmed_model(values):
    good=sorted(v for v in values if v is not None and K['kMinMillivolts']<=v<=K['kMaxMillivolts'])
    if len(good)<12: return None
    trim=len(good)//4; kept=good[trim:-trim]
    return (sum(kept)+len(kept)//2)//len(kept)

class Model:
    """Worker-owned voltage estimator model; all times are monotonic ms."""
    def __init__(self):
        self.filtered=0.; self.percent=-1; self.trend=0
        self.charging=False; self.known=False; self.candidate=False
        self.sample_at=self.charge_at=self.candidate_at=self.full_at=-1
        self.settle_until=self.trend_at=0
        self.charged_high=False;self.small_at=-1;self.small_target=-1
    def charge(self,active,now):
        if self.charge_at<0 or now-self.charge_at>K['kChargeStaleMs']:
            self.candidate_at=-1; self.known=False
        self.charge_at=now
        if self.candidate_at<0 or active!=self.candidate:
            self.candidate=active; self.candidate_at=now
        if now-self.candidate_at>=K['kChargeDebounceMs'] and (not self.known or self.charging!=self.candidate):
            self.known=True; self.charging=self.candidate
            self.settle_until=now+K['kSettleMs']; self.trend=0; self.trend_at=now; self.full_at=-1
    def voltage(self,mv,now):
        if not K['kMinMillivolts']<=mv<=K['kMaxMillivolts']: return
        seed=self.sample_at<0 or now-self.sample_at>K['kStaleMs']
        if seed: self.filtered=f32(mv); self.full_at=-1;self.charged_high=False;self.small_at=-1
        else:
            dt=clamp(now-self.sample_at,0,30000)
            self.filtered=f32(self.filtered+f32(f32(mv-self.filtered)*f32(f32(dt)/f32(K['kFilterMs']+dt))))
        self.sample_at=now; voltage=int(self.filtered+0.5)
        known=self.known and now-self.charge_at<=K['kChargeStaleMs']
        if voltage<4100: self.charged_high=False
        if known and self.charging and voltage>=K['kFullMillivolts']: self.charged_high=True
        threshold=K['kFullMillivolts'] if self.charged_high else K['kBootFullMillivolts']
        if known and not self.charging and voltage>=threshold:
            if self.full_at<0: self.full_at=now
        else: self.full_at=-1
        target=percent_for_voltage(voltage)
        if self.full_at>=0 and now-self.full_at>=K['kFullHoldMs']: target=100
        if not known or self.charging or self.full_at<0 or now-self.full_at<K['kFullHoldMs']: target=min(target,99)
        if seed: self.percent=target; self.trend=0; self.trend_at=now; return
        if known and self.charging: self.percent=min(self.percent,99)
        if now<self.settle_until: self.trend=0; self.trend_at=now; return
        difference=target-self.percent
        endpoint=target in (0,100) and difference!=0
        if difference==0: self.trend=0;self.trend_at=now;self.small_at=-1;return
        if not endpoint and -2<difference<2:
            if self.small_at<0 or self.small_target!=target: self.small_target=target;self.small_at=now
            if now-self.small_at<K['kSmallChangeHoldMs']: self.trend=0;self.trend_at=now;return
        else: self.small_at=-1
        direction=1 if difference>0 else -1
        if direction!=self.trend: self.trend=direction; self.trend_at=now; return
        interval=K['kFallMs'] if direction<0 else K['kRiseChargeMs'] if known and self.charging else K['kRiseRestMs']
        if now-self.trend_at<interval: return
        self.percent=clamp(self.percent+(1 if direction>0 else max(difference,-3)),0,100)
        self.trend_at=now
    def snapshot(self,now):
        valid=self.sample_at>=0 and now-self.sample_at<=K['kStaleMs']
        known=self.known and self.charge_at>=0 and now-self.charge_at<=K['kChargeStaleMs']
        return dict(valid=valid,percent=self.percent if valid else -1,mv=int(self.filtered+0.5) if valid else 0,
                    known=known,charging=known and self.charging)

def feed(model,start,end,voltage,charge=False,period=5000):
    result=[]
    for t in range(start,end+1,1000):
        model.charge(charge(t) if callable(charge) else charge,t)
        if t%period==0:
            v=voltage(t) if callable(voltage) else voltage
            if v is not None: model.voltage(v,t)
        result.append((t,model.snapshot(t)))
    return result

class BatteryModels(unittest.TestCase):
    def test_curve_is_bounded_monotonic_and_hits_reference_points(self):
        values=[percent_for_voltage(v) for v in range(2500,4501)]
        self.assertEqual(values,sorted(values)); self.assertEqual(values[0],0); self.assertEqual(values[-1],100)
        self.assertTrue(all(a[0]<b[0] and a[1]<b[1] for a,b in zip(CURVE,CURVE[1:])))
        for mv,pct in CURVE: self.assertEqual(percent_for_voltage(mv),pct)
    def test_trim_rejects_outliers_before_average(self):
        self.assertEqual(trimmed_model([3801]*12+[2601,2802,4302,4449]),3801)
        self.assertEqual(trimmed_model([3801]*12+[None,0,6000,-1]),3801)
    def test_incomplete_batch_does_not_invent_zero(self):
        for values in ([None]*16,[0]*16,[3801]*11+[None]*5,[6000]*16): self.assertIsNone(trimmed_model(values))
    def test_initial_unknown_then_valid_estimate(self):
        m=Model(); self.assertFalse(m.snapshot(0)['valid']); self.assertEqual(m.snapshot(0)['percent'],-1)
        m.voltage(3800,0); self.assertEqual(m.snapshot(0)['percent'],50)
        self.assertFalse(m.snapshot(0)['known'])
    def test_voltage_failure_preserves_last_value_then_expires(self):
        m=Model(); m.voltage(3800,1000)
        for t in (5000,10000,30000): m.voltage(0,t)
        self.assertEqual(m.snapshot(61000)['percent'],50)
        self.assertFalse(m.snapshot(61001)['valid']); self.assertEqual(m.snapshot(61001)['percent'],-1)
    def test_sample_recovery_reseeds_after_unknown_gap(self):
        m=Model(); m.voltage(4100,0); m.voltage(3500,61000)
        self.assertEqual(m.snapshot(61000)['mv'],3500); self.assertEqual(m.snapshot(61000)['percent'],5)
    def test_debounce_rejects_short_charger_edges(self):
        m=Model()
        for t in range(0,6000,500): m.charge(t%1000==0,t)
        self.assertFalse(m.snapshot(6000)['known'])
        m.charge(True,6000); m.charge(True,7000); self.assertFalse(m.snapshot(7000)['known'])
        m.charge(True,8000); self.assertTrue(m.snapshot(8000)['charging'])
    def test_charger_high_means_not_charging_not_usb_or_full(self):
        m=Model(); feed(m,0,10000,3800,False)
        s=m.snapshot(10000); self.assertTrue(s['known']); self.assertFalse(s['charging']); self.assertEqual(s['percent'],50)
    def test_charge_freshness_and_gap_require_new_debounce(self):
        m=Model(); feed(m,0,10000,3800,True)
        self.assertFalse(m.snapshot(15001)['known'])
        m.charge(True,17000); self.assertFalse(m.snapshot(17000)['known'])
        m.charge(True,18000); m.charge(True,19000); self.assertTrue(m.snapshot(19000)['charging'])
    def test_stable_voltage_does_not_drift(self):
        for period in (5000,15000):
            m=Model(); samples=feed(m,0,600000,3800,period=period)
            self.assertTrue(all(s['percent']==50 for _,s in samples))
    def test_small_noise_does_not_flicker_percent(self):
        m=Model(); samples=feed(m,0,600000,lambda t:3800+(3 if t//5000%2 else -3))
        self.assertLessEqual(max(s['percent'] for _,s in samples)-min(s['percent'] for _,s in samples),1)
    def test_single_load_pulse_does_not_cause_large_drop(self):
        m=Model(); feed(m,0,60000,4000)
        samples=feed(m,61000,150000,lambda t:3700 if 65000<=t<70000 else 4000)
        self.assertGreaterEqual(min(s['percent'] for _,s in samples),74)
    def test_sustained_low_voltage_is_not_hidden_by_filter(self):
        m=Model(); feed(m,0,60000,4100); samples=feed(m,61000,700000,3300)
        self.assertEqual(samples[-1][1]['percent'],0)
        self.assertTrue(all(a[1]['percent']>=b[1]['percent'] for a,b in zip(samples,samples[1:])))
    def test_charger_insertion_cannot_jump_to_full(self):
        m=Model(); feed(m,0,60000,3800)
        samples=feed(m,61000,120000,4200,True)
        self.assertLessEqual(samples[-1][1]['percent'],52)
        self.assertTrue(all(s['percent']<=99 for _,s in samples))
    def test_booting_on_charger_never_reports_full(self):
        m=Model(); samples=feed(m,0,3600000,4200,True)
        self.assertTrue(all(s['percent']<=99 for _,s in samples))
    def test_full_needs_stable_noncharging_high_voltage(self):
        m=Model(); feed(m,0,120000,4200)
        self.assertEqual(m.percent,99)
        feed(m,121000,300000,4200); self.assertEqual(m.percent,100)
        feed(m,301000,330000,4200,True); self.assertLessEqual(m.percent,99)
    def test_rest_voltage_recovery_is_slow(self):
        m=Model(); feed(m,0,60000,3700)
        samples=feed(m,61000,360000,3900)
        self.assertLessEqual(samples[-1][1]['percent']-20,3)
    def test_rise_and_fall_are_rate_limited_with_random_inputs(self):
        rng=random.Random(20260916); m=Model(); m.voltage(3800,0)
        previous=m.percent; changed_at=0
        for t in range(1000,600001,1000):
            m.charge(t<300000,t)
            if t%5000: continue
            m.voltage(rng.randrange(3000,4400),t)
            delta=m.percent-previous
            self.assertGreaterEqual(delta,-3); self.assertLessEqual(delta,1)
            self.assertTrue(0<=m.percent<=100)
            if delta: self.assertGreaterEqual(t-changed_at,K['kFallMs']); changed_at=t
            previous=m.percent
    def test_dark_sampling_uses_elapsed_time_not_sample_count(self):
        a,b=Model(),Model(); feed(a,0,60000,3800); feed(b,0,60000,3800)
        feed(a,61000,240000,3900,period=5000); feed(b,61000,240000,3900,period=15000)
        self.assertLessEqual(abs(a.filtered-b.filtered),2)
        self.assertLessEqual(abs(a.percent-b.percent),1)

class BatteryPowerContracts(unittest.TestCase):
    def test_hardware_pins_match_touch_vendor_reference(self):
        vendor=(REFERENCE/'ESP-IDF-5.5.1/01_factory/components/esp_bsp/bsp_power_manager.c').read_text(encoding='utf-8-sig')
        for token in ('charging_pin_ = GPIO_NUM_3','bat_adc_pin_ = GPIO_NUM_1','gpio_get_level(charging_pin_) == 0'):
            self.assertIn(token,vendor)
        s=read('main/core/battery_mgr.cpp')
        for token in ('kChargePin=GPIO_NUM_3','kChannel=ADC_CHANNEL_0','kDividerRatio=3','GPIO_PULLUP_ENABLE','gpio_get_level(kChargePin)==0'):
            self.assertIn(token,s)
        conflicts=[]
        for p in (ROOT/'main').rglob('*.cpp'):
            if p.name!='battery_mgr.cpp' and re.search(r'GPIO_NUM_3\b',p.read_text(encoding='utf-8')): conflicts.append(p.name)
        self.assertEqual(conflicts,[])
    def test_adc_calibration_robust_batch_and_failure_path(self):
        s=read('main/core/battery_mgr.cpp')
        for token in ('adc_cali_create_scheme_curve_fitting','adc_cali_raw_to_voltage','kSamples=16','kMinimumSamples=12',
                      'if(count>=kMinimumSamples)','std::sort(values,values+count)','const int trim=count/4',
                      's_estimator.observeVoltage((total+kept/2)/kept,now)'):
            self.assertIn(token,s)
        sample=body(s,'void Battery::sample()')
        self.assertEqual(sample.count('observeVoltage('),1)
        self.assertNotRegex(sample,r'observeVoltage\(0|s_published\s*=')
    def test_estimator_source_has_guards_matching_model(self):
        for token in ('sampleAt<0 || now-sampleAt>kStaleMs','std::clamp<int64_t>(now-sampleAt,0,30000)',
                      'difference>-2 && difference<2','std::max(difference,-3)','known && charging?kRiseChargeMs:kRiseRestMs',
                      'now-fullAt<kFullHoldMs','percent=std::min(percent,99)', 'now-chargeAt>kChargeStaleMs'):
            self.assertIn(token,ESTIMATOR)
        self.assertNotRegex(code_only(ESTIMATOR),r'new\b|malloc\(|pow\(|log\(')
    def test_snapshot_publication_is_locked_and_readers_expire_data(self):
        s=read('main/core/battery_mgr.cpp')
        publish=body(s,'void publish('); snap=body(s,'BatterySnapshot Battery::snapshot()')
        self.assertIn('lock(s_snapshotMutex)',publish); self.assertIn('lock(s_snapshotMutex)',snap)
        for token in ('kStaleMs','kChargeStaleMs','out.percent=-1','out.chargingKnown=false'): self.assertIn(token,snap)
        self.assertNotRegex(snap,r'adc_\w+\(|gpio_get_level\(')
        for p in (ROOT/'main/apps').glob('*.cpp'):
            self.assertNotRegex(p.read_text(encoding='utf-8'),r'Battery::(?:poll|sample)\(')
    def test_worker_only_sampling_schedules_and_screen_recovery(self):
        s=read('main/core/battery_mgr.cpp')
        for token in ('screenOn?5000:15000','screenOn && !s_wasScreenOn','s_wasScreenOn=screenOn','else publish(now)'):
            self.assertIn(token,s)
        self.assertIn('nextBattery=now+1000000LL; Battery::poll(Power::isScreenOn())',read('main/core/system_service.cpp'))
    def test_status_and_settings_share_snapshot_and_correct_icons(self):
        w=read('main/ui/window_mgr.cpp'); s=read('main/apps/settings.cpp')
        self.assertIn('Battery::snapshot()',w); self.assertIn('Battery::snapshot()',s)
        for icon in ('BATTERY_FULL','BATTERY_3','BATTERY_2','BATTERY_1','BATTERY_EMPTY','CHARGE'):
            self.assertIn('LV_SYMBOL_'+icon,w)
        self.assertIn('battery.valid',w); self.assertIn('battery.chargingKnown',w)
        self.assertIn('电池估算',s); self.assertIn('充电状态未知',s); self.assertNotIn('已充满',s)
        self.assertNotIn('powerOff',read('main/core/battery_mgr.cpp'))
    def test_added_icons_only_extend_existing_font_metrics_list(self):
        before=(BACKUP/'main/ui/theme.cpp').read_text(encoding='utf-8-sig')
        expected=before.replace('LV_SYMBOL_BATTERY_FULL LV_SYMBOL_BELL LV_SYMBOL_BLUETOOTH',
            'LV_SYMBOL_BATTERY_FULL LV_SYMBOL_BATTERY_1 LV_SYMBOL_BATTERY_2 LV_SYMBOL_BATTERY_3\n        LV_SYMBOL_BELL LV_SYMBOL_BLUETOOTH')
        expected=expected.replace('LV_SYMBOL_AUDIO LV_SYMBOL_BACKSPACE','LV_SYMBOL_UP LV_SYMBOL_DOWN LV_SYMBOL_MUTE LV_SYMBOL_VOLUME_MID\n        LV_SYMBOL_AUDIO LV_SYMBOL_BACKSPACE')
        self.assertEqual(read('main/ui/theme.cpp'),expected)
        font=read('managed_components/lvgl__lvgl/src/font/lv_font_montserrat_18.c')
        self.assertIn('62016,62017,62018,62019,62020',font)
    def test_root_is_fullscreen_subpages_keep_navigation_and_touch_sizes(self):
        s=read('main/apps/settings.cpp')
        self.assertIn('const int bodyTop=page==Page::Root?0:c::kHeader+1',s)
        header=body(s,'if(page!=Page::Root)')
        self.assertIn('c::panel(v.page,c::kWidth,c::kHeader)',header)
        self.assertIn('backCb,this,40,c::kHeader',header)
        self.assertIn('c::kHeight-bodyTop,true',s); self.assertIn('lv_obj_set_pos(v.body,0,bodyTop)',s)
        self.assertIn('case Page::Root: return false',s)
        self.assertIn('kRow=40',read('main/ui/compact_ui.hpp'))
        self.assertEqual(6*40+5,245) # slight scroll, not undersized touch rows
    def test_empty_button_regression_uses_actual_nonempty_caption(self):
        self.assertIn('if(text && *text)',read('main/ui/compact_ui.cpp'))
        for app in ('text_reader','photo_viewer'):
            s=read(f'main/apps/{app}.cpp')
            self.assertLess(s.index('caption.empty()?"?":caption.c_str()'),s.index('lv_obj_get_child(btn,0)'))
            self.assertNotIn('button(m_list,"",fileCb',s)
    def test_pm_init_is_conservative_if_work_lock_unavailable(self):
        s=read('main/core/power_mgr.cpp'); init=body(s,'void Power::init()')
        self.assertLess(init.index('&workloadLock'),init.index('esp_pm_configure'))
        self.assertIn('if(err==ESP_OK)',init); self.assertIn('dfsReady=err==ESP_OK',init)
        self.assertIn('config.max_freq_mhz=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ',init)
        self.assertIn('config.min_freq_mhz=80',init); self.assertIn('config.light_sleep_enable=false',init)
        self.assertIn('if(!hold && !dfsReady) return true',s)
    def test_screen_frequency_lock_is_serialized_and_wakes_before_light(self):
        s=read('main/core/power_mgr.cpp'); transition=body(s,'void setScreenLocked(')
        self.assertLess(transition.index('holdPerformance(true)'),transition.index('screen.store(on)'))
        self.assertIn('if(!on) holdPerformance(false)',transition)
        self.assertNotRegex(transition,r'BleHid|Connectivity|esp_wifi|Settings::set')
        self.assertIn('lock(pmMutex)',body(s,'void holdPerformance(bool hold)'))
        for method in ('Power::WorkGuard::WorkGuard()','Power::WorkGuard::~WorkGuard()'):
            guard=body(s,method); self.assertIn('lock(pmMutex)',guard)
            self.assertNotIn('displayMutex',guard)
    def test_work_guard_balanced_noncopyable_and_fallback_on_failure(self):
        h=read('main/core/power_mgr.hpp'); s=read('main/core/power_mgr.cpp')
        self.assertIn('WorkGuard(const WorkGuard &)=delete',h)
        self.assertIn('operator=(const WorkGuard &)=delete',h)
        self.assertIn('esp_pm_lock_acquire(workloadLock)',s); self.assertIn('esp_pm_lock_release(workloadLock)',s)
        self.assertIn('if(!acquired) return',body(s,'Power::WorkGuard::~WorkGuard()'))
        self.assertIn('dfsReady=false; (void)holdPerformanceLocked(true)',s)
    def test_service_work_guard_releases_before_idle_wait(self):
        s=read('main/core/system_service.cpp')
        block=part(s,'Power::WorkGuard serviceWork;', '} // serviceWork:')
        self.assertIn('Connectivity::poll()',block); self.assertIn('execute(cmd)',block); self.assertIn('Settings::flush()',block)
        self.assertNotIn('vTaskDelay(',code_only(block))
        self.assertLess(s.index('} // serviceWork:'),s.index('vTaskDelay(pdMS_TO_TICKS(standby?100:50))'))
        self.assertLess(s.index('if(Power::sleepTerminal())'),s.index('Power::WorkGuard serviceWork'))
    def test_local_sd_jobs_keep_cpu_lease_after_http_removal(self):
        self.assertFalse((ROOT/'main/core/file_server.cpp').exists())
        self.assertNotIn('esp_http_server',read('main/CMakeLists.txt'))
        worker=body(read('main/core/storage_browser.cpp'),'void worker(')
        self.assertIn('sys::Power::WorkGuard storageWork',worker)
        self.assertRegex(worker,r'\}\s*--active;\s*vTaskDeleteWithCaps')
    def test_offscreen_updates_skip_only_visual_work(self):
        watch=body(read('main/apps/watchface.cpp'),'void Watchface::tickCb(')
        self.assertLess(watch.index('Power::isScreenOn'),watch.index('updateOrientation'))
        gravity=body(read('main/apps/gravity_ball.cpp'),'void GravityBall::tick(')
        self.assertLess(gravity.index('Power::isScreenOn'),gravity.index('readAcceleration'))
        tick=body(read('main/ui/sysinfo_overlay.cpp'),'void tick(')
        self.assertLess(tick.index('Power::isScreenOn'),tick.index('esp_get_free_heap_size'))
        self.assertNotIn('apps::Stopwatch',read('main/main.cpp')) # explicitly removed by the HID/UI task
    def test_accel_only_matches_vendor_bit_and_preserves_range_rate(self):
        ref=(REFERENCE/'Arduino-3.2.0/libraries/SensorLib/src/SensorQMI8658.hpp').read_text(encoding='utf-8-sig')
        self.assertIn('setRegisterBit(QMI8658_REG_CTRL7, 0)',part(ref,'bool enableAccelerometer()', 'bool disableAccelerometer'))
        self.assertIn('setRegisterBit(QMI8658_REG_CTRL7, 1)',part(ref,'bool enableGyroscope()', 'bool disableGyroscope'))
        s=read('main/core/qmi8658.cpp')
        self.assertIn('kAccelEnable = 0x01',s)
        self.assertEqual(len(re.findall(r'writeRegister\(kCtrl7,[^\n]*kAccelEnable',s)),2)
        self.assertIn('writeRegister(kCtrl7, 0x00)',s)
        self.assertIn('writeRegister(kCtrl2, 0x95)',s); self.assertIn('kAccelScale = 4096.0f',s)
    def test_new_api_calls_exist_in_selected_local_headers(self):
        checks={
            'components/esp_pm/include/esp_pm.h':['esp_pm_lock_create','esp_pm_lock_acquire','esp_pm_lock_release','ESP_PM_CPU_FREQ_MAX'],
            'components/esp_adc/include/esp_adc/adc_cali_scheme.h':['adc_cali_create_scheme_curve_fitting'],
            'components/esp_driver_gpio/include/driver/gpio.h':['gpio_get_level','gpio_config'],
        }
        for name,tokens in checks.items():
            s=(SDK/name).read_text(encoding='utf-8')
            for token in tokens: self.assertRegex(s,rf'\b{token}\b')
    def test_old_build_and_dependencies_untouched(self):
        from check_manual_lifecycle import verify_build_unchanged,verify_config_unchanged
        verify_build_unchanged(self);verify_config_unchanged(self)
    def test_existing_radios_input_sos_key_and_storage_codec_preserved(self):
        # BLE startup changed by CLOCK-HID-BLE; GAP/report preservation is checked separately.
        from check_netdisk_cleanup import verify_power_and_buttons_preserved
        verify_power_and_buttons_preserved(self)
        # Audio log-only changes are compared by check_netdisk_cleanup.
        names=['main/core/charset_conv.cpp','main/apps/settings_wifi.cpp',
            'main/ui/home_screen.cpp',
            'main/ui/boot_animation.cpp','main/apps/flashlight_signal.hpp',
            'main/ui/lv_font_cjk_16.c']
        for name in names:
            expected=(BACKUP/name).read_bytes()
            self.assertEqual((ROOT/name).read_bytes(),expected,name)
    def test_hid_apps_use_shared_radio_state_without_stale_error_text(self):
        for name in ('main/apps/bt_keyboard_mouse.cpp','main/apps/bt_music.cpp'):
            src=read(name)
            self.assertIn('SystemService::bluetooth()',src)
            self.assertIn('BleHid::remoteConnected()',src)
            self.assertIn('state.hasError()',src)
            self.assertNotIn('stateText(',src)
            self.assertNotIn('Connecting...',src)

if __name__=='__main__': unittest.main(verbosity=2)
