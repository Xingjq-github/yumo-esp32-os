"""Offline source-contract checks. Does NOT compile, configure, flash or prove runtime behavior.
Run: python -B tools/check_firmware_refactor.py
"""
from pathlib import Path
import hashlib
import json
import re
import unittest

ROOT = (Path(__file__).resolve().parents[2]/'firmware')
MAIN = ROOT / 'main'
BACKUP = ROOT / '../tooling/firmware/fixtures/contracts/system-refactor-20260916'
from build_firmware import SDK

def read(name):
    return (ROOT/name).read_text(encoding='utf-8-sig')

def code_only(text):
    """Remove C/C++ comments and literals; retain punctuation for balance checks."""
    out=[]; i=0
    while i<len(text):
        if text.startswith('//',i):
            end=text.find('\n',i); i=len(text) if end<0 else end; continue
        if text.startswith('/*',i):
            end=text.find('*/',i+2)
            if end<0: raise AssertionError('Unclosed block comment')
            i=end+2; continue
        if text.startswith('R"',i):
            begin=text.find('(',i+2)
            if begin<0: raise AssertionError('Unclosed raw literal')
            delimiter=text[i+2:begin]; end=text.find(')'+delimiter+'"',begin)
            if end<0: raise AssertionError('Unclosed raw literal')
            out.append(' '); i=end+len(delimiter)+2; continue
        if text[i] in ('"',"'"):
            quote=text[i]; i+=1
            while i<len(text):
                if text[i]=='\\': i+=2; continue
                if text[i]==quote: i+=1; break
                i+=1
            else: raise AssertionError('Unclosed quoted literal')
            out.append(' '); continue
        out.append(text[i]); i+=1
    return ''.join(out)

class RefactorContracts(unittest.TestCase):
    def test_ble_name_is_set_once_before_host_start(self):
        text=read('main/core/ble_hid.cpp')
        self.assertEqual(text.count('ble_svc_gap_device_name_set('),1)
        init=text.split('esp_err_t BleHid::init(void)',1)[1].split('esp_err_t BleHid::setEnabled',1)[0]
        self.assertLess(init.index('esp_hidd_dev_init('),init.index('ble_svc_gap_device_name_set('))
        self.assertLess(init.index('ble_svc_gap_device_name_set('),init.index('xTaskCreateStaticPinnedToCore('))
        advertising=text.split('void BleHid::startAdvertisingFromCallback(void)',1)[1]
        self.assertNotIn('ble_svc_gap_device_name_set(',advertising)
        self.assertNotIn('verifyStatusHeap',read('main/ui/window_mgr.cpp'))

    def test_ble_disconnect_has_single_gap_owner(self):
        text=read('main/core/ble_hid.cpp')
        callback=text.split('case ESP_HIDD_DISCONNECT_EVENT:',1)[1].split('default:',1)[0]
        self.assertNotIn('setConnectionHandle(',callback)
        self.assertNotIn('startAdvertisingFromCallback(',callback)
        self.assertIn('rc==0 || rc==BLE_HS_EALREADY',text)
        self.assertIn('auto fields=s_advFields;',text)

    def test_worker_stack_budget_without_periodic_diagnostics(self):
        text=read('main/core/system_service.cpp')
        self.assertIn('constexpr uint32_t kWorkerStackBytes=16*1024;',text)
        self.assertIn('"sys_service",kWorkerStackBytes',text)
        self.assertNotIn('uxTaskGetStackHighWaterMark',text)
        self.assertNotIn('nextDiagnostics',text)
        self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',text)

    def test_passive_freeze_monitor_removed_but_emergency_pwr_retained(self):
        text=read('main/main.cpp')
        for removed in ('freeze_monitor_task','freeze_mon','s_ui_heartbeat','vTaskListTasks','xTaskGetHandle'):
            self.assertNotIn(removed,text)
        self.assertIn('lv_timer_create(ui_pump_cb,20,nullptr)',text)
        buttons=read('main/core/button_mgr.cpp')
        for protected in ('pwrWatchdogTask','kPollMs   = 50','kHoldMs   = 3000','Power::powerOff()'):
            self.assertIn(protected,buttons)

    def test_cmake_source_files_exist_and_are_unique(self):
        sources=re.findall(r'"([^"\n]+\.(?:c|cpp))"',read('main/CMakeLists.txt'))
        self.assertEqual(len(sources),len(set(sources)))
        for source in sources: self.assertTrue((MAIN/source).is_file(),source)
        self.assertEqual(len(sources),48)
        self.assertEqual(sources.count("core/storage_browser.cpp"),1)

    def test_authorized_merge_and_removal_preserve_other_app_order(self):
        before=re.findall(r'make_unique<apps::(\w+)>',(BACKUP/'main/main.cpp').read_text(encoding='utf-8'))
        after=re.findall(r'make_unique<apps::(\w+)>',read('main/main.cpp'))
        expected=[('BtKeyboardMouse' if name=='BtKeyboard' else name)
                  for name in before if name not in ('Stopwatch','BtMouse')]+['TouchScope','PasswordVault']
        self.assertEqual(expected,after); self.assertEqual(len(after),10)
        for text in MAIN.rglob('*'):
            if text.is_file() and text.suffix in ('.cpp','.hpp'):
                self.assertNotRegex(text.read_text(encoding='utf-8'),r'\b(?:Calculator|MusicPlayer|music_player|calculator)\b')

    def test_cpp_delimiters(self):
        for p in MAIN.rglob('*'):
            if p.suffix not in ('.hpp','.cpp'): continue
            code=code_only(p.read_text(encoding='utf-8-sig')); stack=[]
            for ch in code:
                if ch in '([{': stack.append(ch)
                if ch in ')]}':
                    self.assertTrue(stack,f'{p}: unmatched {ch}')
                    self.assertEqual(stack.pop(),{')':'(',']':'[','}':'{'}[ch],str(p))
            self.assertFalse(stack,str(p))

    def test_settings_methods_have_one_definition(self):
        header=read('main/apps/settings.hpp')
        expected=re.findall(r'(?:void|bool|lv_obj_t\s*\*)\s*(\w+)\([^;{}]*\)\s*(?:override)?\s*;',header)
        definitions=[]
        for p in (MAIN/'apps').glob('settings*.cpp'):
            definitions+=re.findall(r'\bSettings::(\w+)\s*\(',code_only(p.read_text(encoding='utf-8')))
        for name in expected: self.assertEqual(definitions.count(name),1,name)

    def test_no_driver_or_flash_work_in_settings(self):
        forbidden=r'\b(?:esp_wifi_\w+|esp_netif_sntp_\w+|nvs_\w+|vTaskDelay|httpd_\w+)\s*\('
        for p in (MAIN/'apps').glob('settings*.cpp'):
            text=code_only(p.read_text(encoding='utf-8'))
            self.assertNotRegex(text,forbidden,str(p))
            self.assertNotRegex(text,r'BleHid::(?:setEnabled|bonds|removeBond|startScan|stopScan|makeDiscoverable)\s*\(',str(p))

    def test_gui_pump_has_only_bounded_work(self):
        text=read('main/main.cpp')
        pump=text.split('static void ui_pump_cb',1)[1].split('// Registered apps',1)[0]
        self.assertIn('EventBus::dispatch(8,ui::QuickPanel::allowEvent)',pump)
        self.assertNotRegex(pump,r'FileServer|Connectivity::poll|saveClock|nvs_|sntp_')
        self.assertIn('lv_timer_create(ui_pump_cb,20,nullptr)',text)
        self.assertNotIn('vTaskList(buf)',text)

    def test_events_never_dispatch_on_producer_thread(self):
        text=read('main/core/event_bus.cpp')
        publish=text.split('void EventBus::publish',1)[1].split('void EventBus::dispatch',1)[0]
        self.assertIn('xQueueSend',publish)
        self.assertNotIn('handler(',publish)
        self.assertIn('homeRequested.store(true)',publish)
        self.assertIn('slot.id == ids[i]',text)

    def test_lifecycle_and_deferred_back(self):
        self.assertIn('virtual bool onBack',read('main/ui/app.hpp'))
        self.assertIn('m_active->onBack()',read('main/ui/window_mgr.cpp'))
        text=read('main/apps/settings.cpp')
        self.assertIn('nextPage=target; pagePending=true',text)
        self.assertIn('lv_timer_delete(timer)',text)
        self.assertIn('if(pagePending)',text)
        self.assertNotIn('lv_async_call',text)

    def test_wifi_scan_completion_zero_results_and_timeout(self):
        text=read('main/core/connectivity.cpp')
        self.assertIn('WIFI_EVENT_SCAN_DONE',text)
        self.assertIn('state.scan=err==ESP_OK?WifiScanState::Complete',text)
        self.assertNotRegex(text,r'if\s*\(num\s*==\s*0\)\s*return false')
        self.assertIn('esp_wifi_clear_ap_list()',text)
        self.assertIn('now>=scanDeadline',text)
        self.assertIn('state.retry<3',text)
        self.assertIn('if(ignoreDisconnect || !reconnect || !connectionActive || state.state!=WifiState::Connecting) continue',text)

    def test_credentials_saved_only_after_got_ip(self):
        text=read('main/core/connectivity.cpp')
        self.assertLess(text.index('RadioEvent::GotIp) {'),text.index('Settings::rememberWifi'))
        self.assertEqual(text.count('Settings::rememberWifi('),1)
        for p in (MAIN/'apps').glob('settings*.cpp'):
            self.assertNotIn('setWifiCredentials(',p.read_text(encoding='utf-8'))

    def test_nvs_batched_and_retryable(self):
        text=read('main/core/settings_store.cpp')
        # Ordinary preferences remain batched/retryable. Theme selection is an
        # explicit worker-only transaction and must not silently retry on failure.
        flush=text.split('bool Settings::flush(',1)[1].split('themes::Id Settings::theme()',1)[0]
        self.assertEqual(flush.count('nvs_commit('),1)
        self.assertEqual(text.count('nvs_commit('),2)
        self.assertIn('now-lastChange < 500000',text)
        self.assertIn('now-dirtySince < 5000000',text)
        self.assertIn('dirty |= bits',text)
        self.assertIn('retryAt = now + 2000000',text)
        self.assertNotIn('writeInt(',text)

    def test_snapshot_locks_separate_from_driver_lock(self):
        text=read('main/core/connectivity.cpp')
        getter=text.split('WifiSnapshot Connectivity::snapshot()',1)[1]
        self.assertIn('lock(viewMutex)',getter)
        self.assertNotIn('lock(mutex)',getter)
        self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',text)
        service=read('main/core/system_service.cpp')
        self.assertIn('xTaskCreateWithCaps',service)
        self.assertNotRegex(code_only(service),r'\blv_\w+\s*\(')

    def test_compact_layout_and_touch_geometry(self):
        text=read('main/ui/compact_ui.hpp')
        self.assertIn('kWidth=240',text); self.assertIn('kHeader=36',text)
        self.assertIn('kRow=40',text); self.assertIn('kTouch=36',text)
        self.assertIn('kInput=TextInputLayout::Height',text)
        self.assertEqual(119+1+120,240)
        self.assertEqual(sum([79,79,80])+2,240)
        self.assertGreaterEqual(min([40,48,52]),36)
        self.assertEqual(14+2*9,32) # slider hit region, not the visual track
        self.assertLessEqual(34+14+9,60)
        self.assertIn('kEdgePad = 0',read('main/ui/theme.hpp'))

    def test_wifi_input_and_ble_role_preserved(self):
        text=read('main/apps/settings_wifi.cpp')
        self.assertIn('lv_textarea_set_password_mode(v.password,false)',text)
        self.assertNotIn("buildInputBar",text)
        shared=read("main/ui/text_input.cpp")
        self.assertIn('c::button(actions,"输入"',shared)
        self.assertIn('c::button(actions,"删除"',shared)
        self.assertNotIn("退出",shared)
        bt=read('main/apps/settings_bluetooth.cpp')
        self.assertIn('SystemService::connectKeyboard(self->scanned[index])',bt)
        self.assertIn('同时只能连接一个设备',bt)
        self.assertIn('Dialog::ForgetBond',bt)
        self.assertIn('Dialog::ForgetAll',bt)
        service=read('main/core/system_service.cpp')
        self.assertIn('BleHid::confirmPairing(cmd.value,cmd.pairGeneration)',service)

    def test_lvgl_calls_exist_in_local_headers(self):
        headers='\n'.join(p.read_text(encoding='utf-8',errors='replace') for p in (ROOT/'../dependencies/managed_components/lvgl__lvgl').rglob('*.h'))
        calls=set()
        for p in list((MAIN/'apps').glob('settings*.cpp'))+[MAIN/'ui/compact_ui.cpp']:
            calls.update(re.findall(r'\b(lv_\w+)\s*\(',code_only(p.read_text(encoding='utf-8'))))
        for call in sorted(calls): self.assertRegex(headers,rf'\b{call}\b',call)

    def test_wifi_constants_exist_in_local_sdk(self):
        self.assertTrue(SDK.is_dir(),'Local SDK required for this read-only interface check')
        headers='\n'.join(p.read_text(encoding='utf-8',errors='replace') for part in ('esp_wifi/include','esp_common/include') for p in (SDK/'components'/part).rglob('*.h'))
        constants=set(re.findall(r'\b(?:WIFI_REASON_|ESP_ERR_WIFI_)\w+',read('main/core/connectivity.cpp')+read('main/apps/settings_wifi.cpp')))
        for constant in constants: self.assertIn(constant,headers,constant)

    def test_existing_hardware_configuration_untouched(self):
        baseline=json.loads((BACKUP/'baseline.json').read_text(encoding='utf-8'))
        bsp_source='components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c'
        bsp_header='components/esp32_s3_touch_lcd_1_54/include/bsp/display.h'
        recent=ROOT/'../tooling/firmware/fixtures/contracts/ui-power-20260916'
        for name,checksum in baseline.items():
            if name in (bsp_source,bsp_header):
                before=(recent/name).read_text(encoding='utf-8')
                after=read(name)
                if name==bsp_source:
                    # Only the new read observer is permitted in the owned BSP.
                    after=after.replace('#include "esp_timer.h"\n','').replace('#include "bsp/touch_observer.h"\n','')
                    after=re.sub(r'// Observe the existing read path.*?(?=esp_err_t bsp_touch_new\()', '',after,flags=re.S)
                    factory='    esp_err_t err = esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, ret_touch);\n    if (err == ESP_OK) {\n        touch_original_read = (*ret_touch)->read_data;\n        (*ret_touch)->read_data = bsp_touch_observed_read;\n    }\n    return err;'
                    self.assertEqual(after.count(factory),1)
                    after=after.replace(factory,'    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, ret_touch);')
                    after=after.replace('#include "esp_lcd_panel_commands.h"\n','')
                    self.assertEqual(after.count('// BEGIN BLE-preserving display standby'),1)
                    after=re.sub(r'// BEGIN BLE-preserving display standby.*?// END BLE-preserving display standby\n\n','',after,flags=re.S)
                    after=after.replace('ESP_LOGD(TAG, "Setting flipped','ESP_LOGI(TAG, "Setting flipped')
                    after=after.replace('return brightness; // stored unit is already percent (0..100)','return brightness * 100 / 255;')
                else:
                    after=after.replace('\n/** Worker-only reversible standby, after GUI has stopped apps. No radio shutdown.\n * On any suspend error, call resume too: hardware may be partially asleep.\n * Backlight PWM must be zero before suspend; routing remains valid for wake.\n */\nesp_err_t bsp_display_suspend(void);\nesp_err_t bsp_display_resume(void);\n','')
                self.assertEqual(before,after,name)
                self.assertEqual(hashlib.sha256((recent/name).read_bytes()).hexdigest(),checksum,name)
            elif name=='sdkconfig.defaults':
                before=(ROOT/'../tooling/firmware/fixtures/contracts/ble-standby-20260916'/name).read_text(encoding='utf-8')
                self.assertEqual(hashlib.sha256((ROOT/'../tooling/firmware/fixtures/contracts/ble-standby-20260916'/name).read_bytes()).hexdigest(),checksum)
                after=read(name).split('\n# BEGIN BLE-preserving standby power policy',1)[0]
                after=after.replace('CONFIG_BT_NIMBLE_ROLE_CENTRAL=y','CONFIG_BT_NIMBLE_ROLE_CENTRAL=n')
                # Sixteen separately paired devices; keep the original configuration otherwise.
                self.assertIn('CONFIG_BT_NIMBLE_MAX_CCCDS=128',after)
                after=after.replace('CONFIG_BT_NIMBLE_MAX_CCCDS=128','CONFIG_BT_NIMBLE_MAX_CCCDS=16')
                self.assertIn('CONFIG_BT_NIMBLE_MAX_BONDS=16',after)
                after=after.replace('CONFIG_BT_NIMBLE_MAX_BONDS=16','CONFIG_BT_NIMBLE_MAX_BONDS=3')
                after=after.replace('CONFIG_ESP_CONSOLE_UART_BAUDRATE=1000000','CONFIG_ESP_CONSOLE_UART_BAUDRATE=2000000')
                self.assertEqual(before,after)
            elif name.startswith('components/') or name in ('main/idf_component.yml','main/ui/lv_font_cjk_16.c'):
                self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),checksum,name)
        # CMake/Kconfig may normalize disabled-option comments; the component
        # manager may refresh its manifest digest after an app was removed.
        # Preserve every enabled/value-bearing setting and all locked package
        # metadata, while allowing these two build-generated normalizations.
        before_config=(BACKUP/'sdkconfig').read_text(encoding='utf-8')
        values=lambda text: dict(re.findall(r'^(CONFIG_\w+)=(.*)$',text,re.MULTILINE))
        before,after=values(before_config),values(read('sdkconfig'))
        allowed={
            'CONFIG_BT_NIMBLE_ROLE_CENTRAL':'y','CONFIG_NIMBLE_ROLE_CENTRAL':'y','CONFIG_BT_NIMBLE_GATT_CLIENT':'y',
            'CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE': '6144',
            'CONFIG_BT_NIMBLE_MAX_CCCDS': '128', 'CONFIG_NIMBLE_MAX_CCCDS': '128',
            'CONFIG_BT_NIMBLE_MAX_BONDS': '16', 'CONFIG_NIMBLE_MAX_BONDS': '16',
            # Kconfig synchronizes these deprecated aliases to the host stack.
            'CONFIG_NIMBLE_TASK_STACK_SIZE': '6144',
            'CONFIG_BT_NIMBLE_TASK_STACK_SIZE': '6144',
            'CONFIG_PM_ENABLE': 'y', 'CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL': '1',
            'CONFIG_BT_CTRL_MODEM_SLEEP': 'y', 'CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1': 'y',
            'CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL': 'y',
            'CONFIG_BT_CTRL_SLEEP_MODE_EFF': '1', 'CONFIG_BT_CTRL_SLEEP_CLOCK_EFF': '1',
            # IDF console Kconfig caps UART baud at 1M when PM is enabled.
            'CONFIG_ESP_CONSOLE_UART_BAUDRATE': '1000000', 'CONFIG_CONSOLE_UART_BAUDRATE': '1000000',
            'CONFIG_ESPTOOLPY_MONITOR_BAUD': '1000000', 'CONFIG_MONITOR_BAUD': '1000000',
        }
        for key,value in allowed.items(): self.assertEqual(after.get(key),value,key)
        self.assertEqual({k:v for k,v in before.items() if k not in allowed},
                         {k:v for k,v in after.items() if k not in allowed})
        before_lock=(BACKUP/'dependencies.lock').read_text(encoding='utf-8')
        normalize=lambda text: re.sub(r'^manifest_hash:.*\n','',text,flags=re.MULTILINE)
        self.assertEqual(normalize(before_lock),normalize(read('dependencies.lock')))

    def test_archived_legacy_files_stay_removed_after_authorized_cleanup(self):
        # Historical backup retention was explicitly revoked on 2026-09-20.
        # Keep the removal inventory, not the retired application source files.
        baseline=json.loads((BACKUP/'baseline.json').read_text(encoding='utf-8'))
        # Directory placeholders are not retired production sources.
        archived=[name for name in baseline if name.startswith('main/') and Path(name).name!='.gitkeep' and not (ROOT/name).exists()]
        latest={f'main/apps/{app}.{ext}' for app in ('bt_keyboard','bt_mouse','stopwatch') for ext in ('cpp','hpp')}
        netdisk={'main/core/file_server.cpp','main/core/file_server.hpp'}
        removed_games={f'main/apps/{app}.{ext}' for app in ('gravity_ball','touch_scope') for ext in ('cpp','hpp')} & set(baseline)
        historical=[name for name in archived if name not in latest|netdisk|removed_games]
        self.assertEqual(len(historical),17)
        self.assertEqual(set(archived),set(historical)|latest|netdisk|removed_games)
        cmake=read('main/CMakeLists.txt')
        for name in archived:
            self.assertFalse((ROOT/name).exists(),name)
            self.assertNotIn('"'+name.removeprefix('main/')+'"',cmake,name)
            self.assertFalse((BACKUP/'removed'/name).exists(),name)

    def test_cancelled_wifi_scan_has_terminal_fence(self):
        text=read('main/core/connectivity.cpp')
        start=text.split('esp_err_t Connectivity::startScan()',1)[1].split('void Connectivity::cancelScan',1)[0]
        self.assertLess(start.index('scanOutstanding || awaitingStop'),start.index('esp_wifi_scan_start'))
        callback=text.split('void radioEvent(',1)[1].split('void tuneRadio()',1)[0]
        self.assertIn('scanCompletion.store',callback)
        self.assertIn('stopDelivered.store(true)',callback)
        self.assertIn('scanCompletion.exchange(-1)',text)
        recovery=text.split('esp_err_t restartStationLocked(',1)[1].split('esp_err_t beginConnect()',1)[0]
        self.assertLess(recovery.index('if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_STARTED) return err'),recovery.index('awaitingStop=true'))
        self.assertIn('restartStationLocked(now)',text.split('void Connectivity::poll()',1)[1])
        self.assertIn('transitionDeadline=now+4000000',text)
        self.assertIn('cancelDeadline=esp_timer_get_time()+2000000',text)
        self.assertIn('view.scanPending=scanOutstanding || awaitingStop',text)
        self.assertIn('targetSsid!=event.ssid',text)

    def test_pairing_identity_is_coherent_and_claimed_once(self):
        text=read('main/core/ble_hid.cpp')
        confirm=text.split('esp_err_t BleHid::confirmPairing',1)[1].split('void BleHid::setStateFromCallback',1)[0]
        self.assertIn('request.generation==expectedGeneration',confirm)
        self.assertLess(confirm.index('s_pairRequest.action=BlePairAction::None'),confirm.index('ble_sm_inject_io'))
        self.assertNotIn('clearPairRequest()',confirm)
        snapshot=text.split('BlePairRequest BleHid::pairing(void)',1)[1].split('esp_err_t BleHid::confirmPairing',1)[0]
        self.assertIn('portENTER_CRITICAL(&s_pairLock)',snapshot)
        ui=read('main/apps/settings.cpp')+read('main/apps/settings_bluetooth.cpp')
        global_ui=read('main/ui/bluetooth_pairing.cpp')
        self.assertIn('state.pairGeneration==answered',global_ui)
        self.assertIn('answerPairing(accept,generation)',global_ui)
        self.assertIn('BluetoothPairing::poll()',read('main/main.cpp'))

    def test_hid_pointer_published_only_after_success(self):
        text=read('main/core/ble_hid.cpp')
        self.assertIn('std::atomic<esp_hidd_dev_t *> s_device',text)
        init=text.split('esp_err_t BleHid::init(void)',1)[1].split('esp_err_t BleHid::setEnabled',1)[0]
        self.assertIn('hiddEvent, &device)',init)
        self.assertLess(init.index('ble_store_config_init()'),init.index('s_device.store(device)'))
        self.assertLess(init.index('s_device.store(device)'),init.index('xTaskCreateStaticPinnedToCore'))
        self.assertNotIn('&s_device)',init)

    def test_battery_sampling_not_on_gui_or_hid_reader(self):
        text=read('main/core/battery_mgr.cpp')
        getter=text.split('BatterySnapshot Battery::snapshot()',1)[1]
        self.assertIn('lock(s_snapshotMutex)',getter)
        self.assertIn('return snapshot().millivolts/1000.0f',getter)
        self.assertIn('return snapshot().percent',getter)
        self.assertNotIn('adc_',getter)
        self.assertNotIn('gpio_get_level',getter)
        self.assertIn('nextBattery=supplyNow+1000000LL;Battery::poll(Power::isScreenOn())',read('main/core/system_service.cpp'))
        for p in (MAIN/'apps').glob('*.cpp'):
            self.assertNotRegex(p.read_text(encoding='utf-8'),r'Battery::(?:sample|poll)\(')

    def test_backlight_serialized_but_emergency_path_independent(self):
        text=read('main/core/power_mgr.cpp')
        for name in ('screenOn','screenOff','toggleScreen','applyBrightness'):
            body=text.split('void Power::'+name+'()',1)[1] if name!='applyBrightness' else text.split('void Power::applyBrightness(int percent)',1)[1]
            body=body.split('void Power::',1)[0]
            self.assertIn('lock(displayMutex)',body)
        shutdown=text.split('void Power::powerOff()',1)[1]
        self.assertNotIn('lock(displayMutex)',shutdown)
        self.assertIn('gpio_set_level(kBatEnPin,0)',shutdown)
        self.assertNotIn('bsp_display_lock',text)

    def test_primary_radio_pages_leave_room_for_devices(self):
        wifi=read('main/apps/settings_wifi.cpp').split('void Settings::wifiToggleCb',1)[0]
        self.assertIn('button(actions,"手动"',wifi)
        self.assertIn('button(actions,"已保存"',wifi)
        self.assertNotIn('section(v.body,"附近网络")',wifi)
        # Body 203px, toggle/status/action 40+40+36 + 3 gaps: two 40px rows fit.
        self.assertGreaterEqual(203-(40+40+36+3),2*40+1)
        bt=read('main/apps/settings_bluetooth.cpp').split('void Settings::btToggleCb',1)[0]
        self.assertIn('button(actions,"允许发现"',bt)
        self.assertIn('button(actions,"附近设备"',bt)
        self.assertGreaterEqual(203-(40+40+36+24+4),40)

    def test_media_dwell_and_sensor_control_not_in_app_callbacks(self):
        self.assertIn('SystemService::sendConsumer(item->key)',read('main/apps/bt_music.cpp'))
        self.assertIn('BleHid::sendConsumer(cmd.mediaKey)',read('main/core/system_service.cpp'))
        self.assertIn('vTaskDelay(pdMS_TO_TICKS(40))',read('main/core/ble_hid.cpp'))
        self.assertIn('std::atomic<bool> s_ready',read('main/core/qmi8658.cpp'))
        for p in (MAIN/'apps').glob('*.cpp'):
            self.assertNotRegex(code_only(p.read_text(encoding='utf-8')),r'(?:BleHid::sendConsumer|Qmi8658::init|Qmi8658::setEnabled)\s*\(')

    def test_async_messages_own_values(self):
        event=read('main/core/event_bus.hpp').split('struct Event {',1)[1].split('};',1)[0]
        self.assertNotIn('*',event)
        command=read('main/core/system_service.cpp').split('struct Command {',1)[1].split('};',1)[0]
        self.assertNotIn('*',command)
        self.assertIn('char ssid[33]',command)

    def test_psram_worker_flash_safety_configuration(self):
        config=read('sdkconfig'); defaults=read('sdkconfig.defaults')
        for flag in ('CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y'):
            self.assertIn(flag,config); self.assertIn(flag,defaults)
        for flag in ('CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y','CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y','CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y'):
            self.assertIn(flag,config)
        service=read('main/core/system_service.cpp')
        self.assertIn('#if !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || !CONFIG_SPIRAM_RODATA',service)
        self.assertIn('requires flash instructions and rodata in PSRAM',service)

if __name__ == '__main__':
    unittest.main(verbosity=2)
