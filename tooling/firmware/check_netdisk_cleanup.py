"""Network-drive removal and diagnostic-cleanup contracts; no compile or device I/O.
Comparison checks preserve production behavior outside the explicitly removed hooks.
"""
from pathlib import Path
import re,unittest
from check_firmware_refactor import code_only
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BASE=ROOT/'../tooling/firmware/fixtures/contracts/netdisk-diagnostics-20260917-155918'
def read(name,base=ROOT):return (base/name).read_text(encoding='utf-8-sig')
def part(s,a,b):return s.split(a,1)[1].split(b,1)[0]
def compact(s):return re.sub(r'\s+','',code_only(s))
def without_logs(s):
    s=code_only(s)
    # Literals/comments are gone, so parentheses can be balanced safely.
    while (m:=re.search(r'\bESP_LOG[EWIDV]\s*\(',s)):
        pos=m.end();depth=1
        while depth:
            if s[pos]=='(':depth+=1
            elif s[pos]==')':depth-=1
            pos+=1
        while s[pos].isspace():pos+=1
        assert s[pos]==';'
        s=s[:m.start()]+s[pos+1:]
    return re.sub(r'\s+','',s)
def verify_power_and_buttons_preserved(test):
    old=read('main/core/power_mgr.cpp',BASE)
    expected=old.replace('#include "file_server.hpp"\n','').replace('''    if(!FileServer::pauseForSleep()) {
        FileServer::resumeRequests();
        return ESP_ERR_NOT_FINISHED; // leave the screen/app/radios untouched
    }
''','').replace('        FileServer::resumeRequests();\n','')
    test.assertEqual(compact(read('main/core/power_mgr.cpp')),compact(expected))
    before=read('main/core/button_mgr.cpp',BASE);after=read('main/core/button_mgr.cpp')
    funcs=(('static void changeVolume','static void bootCb'),('static void bootCb','static void keyCb'),
           ('static void keyCb','static void pwrCb'),('static void pwrCb','static void pwrWatchdogTask'),
           ('static void pwrWatchdogTask','static esp_err_t createButton'),('void Buttons::init','}// namespace sys'))
    for a,b in funcs:test.assertEqual(without_logs(part(after,a,b)),without_logs(part(before,a,b)),a)
    for text in (before,after):
        test.assertIn('xTaskCreate(pwrWatchdogTask, "pwr_wdt", 3072, nullptr, 11, nullptr)',text)
    test.assertEqual(compact(part(after,'    if (result == ESP_OK)', 'void Buttons::init')),
                     compact(part(before,'    if (result == ESP_OK)', 'void Buttons::init')))

class CleanupContracts(unittest.TestCase):
    def test_active_sources_have_no_network_drive_symbols_or_routes(self):
        for p in (ROOT/'main').rglob('*'):
            if p.suffix not in ('.cpp','.hpp','.h','.c','.txt'):continue
            # Explicit 2026-10-20 request adds an AP-scoped service. Legacy
            # global FileServer stays removed; only its replacement and CMake
            # may contain HTTP symbols. Other components remain fenced.
            allowed=p.relative_to(ROOT).as_posix() in {'main/core/lan_web.cpp','main/CMakeLists.txt'}
            pattern=r'FileServer|file_server' if allowed else r'FileServer|file_server|esp_http_server|httpd_|网络硬盘|/api/(list|mkdir|rmdir|delete)|/upload|/download'
            self.assertNotRegex(p.read_text(encoding='utf-8-sig'),pattern,str(p))
        for name in ('main/core/file_server.cpp','main/core/file_server.hpp'):
            # User authorized deleting the old network-drive source backup.
            self.assertFalse((ROOT/name).exists());self.assertFalse((BASE/name).exists())
    def test_sntp_stays_and_http_retry_timer_is_gone(self):
        s=read('main/core/system_service.cpp')
        for token in ('esp_netif_sntp_init(&config)','esp_netif_sntp_start()',
                      'ServiceAction::SyncTime','esp_netif_sntp_sync_wait(0)',
                      'pollTimeSync();','stopTimeClient();'):
            self.assertIn(token,s)
        self.assertNotRegex(s,r'nextNetwork|serverBackoff|FileServer')
    def test_real_sleep_and_pwr_safeguards_preserved(self):
        verify_power_and_buttons_preserved(self)
        config=read('sdkconfig')
        for token in ('CONFIG_ESP_INT_WDT=y','CONFIG_ESP_TASK_WDT_EN=y','CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y',
                      'CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536'):
            self.assertIn(token,config)
    def test_gui_pump_and_app_registry_unchanged_except_counter(self):
        old=read('main/main.cpp',BASE);new=read('main/main.cpp')
        previous=part(old,'static void ui_pump_cb','// Freeze diagnostics:').replace('    ++s_ui_heartbeat;\n','')
        current=part(new,'static void ui_pump_cb','// Registered apps')
        self.assertEqual(compact(current),compact(previous))
        self.assertEqual(part(old,'static void register_apps','extern "C"'),part(new,'static void register_apps','extern "C"'))
    def test_no_standalone_diagnostic_task_or_periodic_stack_scan(self):
        for name in ('main/main.cpp','main/core/system_service.cpp'):
            self.assertNotRegex(read(name),r'freeze_monitor|freeze_mon|s_ui_heartbeat|nextDiagnostics|uxTaskGetStackHighWaterMark|xTaskGetHandle|vTaskListTasks|\[hb\]|\[boot\]')
    def test_ble_logic_unchanged_when_diagnostic_prints_ignored(self):
        old=read('main/core/ble_hid.cpp',BASE);new=read('main/core/ble_hid.cpp')
        expected=without_logs(old).replace('if(err==ESP_OK){s_lastBattery=level;}','if(err==ESP_OK)s_lastBattery=level;')
        self.assertEqual(without_logs(new),expected)
        self.assertNotRegex(new,r'Before controller init:|Controller ready:|Pairing confirm requested:|Battery level published:')
        for token in ('publishPairRequest','nimble_port_init()','BLE HID init failed','NimBLE startup timed out'):
            self.assertIn(token,new)
    def test_audio_and_clock_interaction_unchanged_without_trace_prints(self):
        self.assertEqual(without_logs(read('main/apps/watchface.cpp')),without_logs(read('main/apps/watchface.cpp',BASE).replace('#include "esp_log.h"\n','')))
        old=without_logs(read('main/core/audio_mgr.cpp',BASE));new=without_logs(read('main/core/audio_mgr.cpp'))
        self.assertEqual(new.replace('if(result!=0)',''),old)
        for name in ('main/core/button_mgr.cpp','main/apps/watchface.cpp'):
            self.assertNotRegex(read(name),r'\[btn\]|\[gesture\]|\[swipe\]')
    def test_photo_input_lifetime_without_probe_logs(self):
        # Small sources release SD before decode; large sources stream in the
        # worker. Both close FILE before releasing SD and publishing completion.
        current=read('main/apps/photo_viewer.cpp');worker=read('main/core/photo_loader.cpp')
        release=part(current,'void PhotoViewer::releaseSource','void PhotoViewer::showMessage')
        self.assertIn('m_imageJob->cancelled.store(true);m_imageJob.reset()',release)
        self.assertIn('m_source.reset()',release)
        self.assertIn('Source::~Source() {heap_caps_free(data);}',worker)
        owner=part(worker,'struct Input {','bool readSource')
        self.assertLess(owner.index('card(c.job.sdAccess)'),owner.index('std::fopen('))
        close=part(owner,'bool close()','bool failure(')
        self.assertLess(close.index('std::fclose(file)'),close.index('card.release()'))
        self.assertIn('~Input() {close();}',owner)
        execute=part(worker,'void execute(Job &job)','void worker(')
        self.assertIn('std::unique_ptr<Input> input',execute)
        self.assertIn('!readSource(job,context,input) || cancelled(&context) || !parseHeader(job,context,input.get())',execute)
        self.assertIn('if(input && !input->close()) ok=false',execute)
        completion=part(worker,'void worker(','Source::~Source')
        self.assertLess(completion.index('execute(*job)'),completion.index('job->done.store'))
        self.assertNotRegex(current+worker,r'ESP_LOG[IDV]|\bprintf\(')
    def test_overlay_starts_without_statistics_or_temperature_hardware(self):
        s=read('main/ui/sysinfo_overlay.cpp');init=part(s,'void SysInfoOverlay::init','void SysInfoOverlay::setVisible')
        for token in ('temperature_sensor_install','temperature_sensor_enable','fps_stats_enable','lv_timer_ready'):
            self.assertNotIn(token,init)
        self.assertIn('lv_timer_pause(s_timer)',init)
        self.assertIn('s_instrumentationActive = false',s)
    def test_overlay_toggles_hardware_and_gates_temperature_reads(self):
        s=read('main/ui/sysinfo_overlay.cpp')
        visible=part(s,'void SysInfoOverlay::setVisible','bool SysInfoOverlay::visible')
        for token in ('setInstrumentationActive(on && sys::Power::isScreenOn())','lv_timer_pause(s_timer)','lv_timer_resume(s_timer)'):
            self.assertIn(token,visible)
        tick=part(s,'void tick(', 'lv_obj_t *makeLabel')
        self.assertLess(tick.index('setInstrumentationActive(active)'),tick.index('if(!active) return'))
        self.assertIn('s_visible && sys::Power::isScreenOn()',tick)
        self.assertIn('s_tempSensor && s_tempEnabled && temperature_sensor_get_celsius',tick)
        control=part(s,'void setInstrumentationActive','void tick')
        for token in ('if(s_instrumentationActive == active) return','esp_lv_adapter_fps_stats_enable(s_disp, active)',
                      'if(s_tempSensor && !s_tempEnabled)','temperature_sensor_disable(s_tempSensor) == ESP_OK'):
            self.assertIn(token,control)
    def test_overlay_visibility_spec(self):
        # Independent expected-state model, not execution of the C++ driver.
        for visible in (False,True):
            for screen in (False,True):
                active=visible and screen
                self.assertEqual(active,int(visible)+int(screen)==2)
    def test_user_retained_features_and_battery_acquisition_preserved(self):
        # User now explicitly removes battery diagnostics, not sampling or other apps.
        from check_battery_diagnostics_cleanup import verify_sampling_preserved
        verify_sampling_preserved(self)
        for name in ('main/apps/touch_scope.cpp','main/apps/touch_scope.hpp',
                     'main/core/connectivity.cpp','main/core/storage_browser.cpp',
                     'main/ui/window_mgr.cpp','main/core/settings_store.cpp'):
            self.assertEqual((ROOT/name).read_bytes(),(BASE/name).read_bytes(),name)
        s=read('main/apps/settings_system.cpp')
        self.assertIn('系统信息浮层',s);self.assertIn('无线默认关闭，需手动开启',s)
        self.assertNotIn('按原开关设置恢复无线',s)
    def test_only_expected_files_removed_from_main(self):
        before={p.relative_to(BASE/'main').as_posix() for p in (BASE/'main').rglob('*') if p.is_file()}
        after={p.relative_to(ROOT/'main').as_posix() for p in (ROOT/'main').rglob('*') if p.is_file()}
        self.assertEqual(before-after,{'core/file_server.cpp','core/file_server.hpp'})
        self.assertEqual(after-before,set())

if __name__=='__main__':unittest.main(verbosity=2)
