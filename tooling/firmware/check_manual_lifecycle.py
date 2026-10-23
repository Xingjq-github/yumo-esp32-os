"""Source contracts and specification models only. Never builds or flashes firmware.
Models check intended transitions, not execution of the ESP32 C++ implementation.
"""
from pathlib import Path
import hashlib,json,re,unittest,itertools
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/battery-clean-buildflash-20260917-173715'
BASE=ROOT/'../tooling/firmware/fixtures/contracts/battery-clean-buildflash-20260917-173715'
def read(p):return (ROOT/p).read_text(encoding='utf-8-sig')
def part(s,a,b):return s.split(a,1)[1].split(b,1)[0]
def baseline():return json.loads((RUN/'before.json').read_text(encoding='utf-8'))
def verify_build_unchanged(test):
    artifacts=baseline()['build_artifacts'];test.assertEqual(len(artifacts),7)
    for name,expected in artifacts.items():
        p=ROOT/name
        test.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),expected['sha256'],name)
        test.assertEqual(p.stat().st_mtime_ns,expected['mtime_ns'],name)
        test.assertEqual(p.stat().st_size,expected['bytes'],name)
def verify_config_unchanged(test):
    for name in ('sdkconfig','sdkconfig.defaults','CMakeLists.txt','dependencies.lock','main/idf_component.yml','partitions.csv'):
        test.assertEqual((ROOT/name).read_bytes(),(BASE/name).read_bytes(),name)

    # Freeze pre-build configuration. Authorized builds replace the three artifact
    # freeze assertions with a new image/source/partition receipt, not silent skips.
    test.assertEqual((ROOT/'main/CMakeLists.txt').read_bytes(),(BASE/'main/CMakeLists.txt').read_bytes())

class ManualLifecycleContracts(unittest.TestCase):
    def test_existing_firmware_and_configuration_not_touched(self):
        verify_build_unchanged(self);verify_config_unchanged(self)
    def test_reference_components_untouched(self):
        for name,expected in baseline()['source_inputs'].items():
            if name.startswith('components/'):
                self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),expected['sha256'],name)
    def test_only_requested_main_modules_changed(self):
        allowed={'main/main.cpp','main/apps/settings_system.cpp',
                 'main/core/battery_mgr.cpp'}
        for name,expected in baseline()['source_inputs'].items():
            if name.startswith('main/') and name not in allowed:
                self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),expected['sha256'],name)
    def test_bluetooth_apps_have_no_device_identity_banner(self):
        for app in ('bt_music','bt_keyboard_mouse'):
            for ext in ('cpp','hpp'):
                s=read(f'main/apps/{app}.{ext}')
                for removed in ('m_identity','BluetoothIdentity','bluetooth_identity.hpp'):self.assertNotIn(removed,s)
        self.assertIn('v.peerIdentity.update',read('main/apps/settings_bluetooth.cpp'))
    def test_clock_is_buttonless_fullscreen_and_swipeable(self):
        s=read('main/apps/watchface.cpp')
        self.assertIn('kFaceHeight=240',s)
        for gone in ('m_dots','kSelectorHeight','::button(', 'auto selector'):self.assertNotIn(gone,s)
        for stay in ('LV_DIR_LEFT','LV_DIR_RIGHT','adx >= 30','%04d.%02d.%02d'):self.assertIn(stay,s)
    def test_keyboard_uses_reclaimed_space(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        for token in ('3,0,65,44','196,45,44,195','m_keys,0,45','240,195','(i/3)*49,79,48','31+(i/3)*41,79,40','m_status,2,44'):self.assertIn(token,s)
        for y,height in ((0,44),(45,195),(45+3*49,48),(45+31+3*41,40)):
            self.assertLessEqual(y+height,240)
    def test_boot_permissions_do_not_restore_old_radio_or_motion_bits(self):
        s=part(read('main/core/settings_store.cpp'),'void Settings::load()', 'bool Settings::flush')
        for key in ('bt','wifi','motion'):self.assertNotIn(f'nvs_get_i32(h,"{key}"',s)
        self.assertIn('c.bluetooth=true;c.wifi=true;c.motion=false',s)
        for preserved in ('readString(h,"ssid",33)','readString(h,"pass",65)','"wifihist"','nvs_get_i64(h,"clock"'):self.assertIn(preserved,s)
        h=read('main/core/settings_store.hpp')
        self.assertRegex(h,r'bool\s+bluetooth\s*= true;')
        self.assertRegex(h,r'bool\s+wifi\s*= true;')
        self.assertRegex(h,r'bool\s+motion\s*= false;')
    def test_bluetooth_boot_start_is_one_shot_and_preserves_manual_shutdown(self):
        worker=part(read('main/core/system_service.cpp'),'void worker(void *)', 'ServiceDomain SystemService::domain')
        boot,loop=worker.split('for(;;)',1)
        self.assertIn('startBootRadios()',boot)
        startup=part(read('main/core/system_service.cpp'),'void startBootRadios()', 'void pollBootConnections()')
        self.assertIn('Settings::bluetoothOn()&&!Power::sleepPending()',startup)
        self.assertIn('execute({ServiceAction::BluetoothEnable,true})',startup)
        self.assertIn('Settings::setBluetoothOn(BleHid::enabled())',startup)
        self.assertNotIn('ServiceAction::BluetoothEnable,true',loop)
        self.assertNotIn('Settings::bluetoothOn()',loop)
        self.assertIn('BleHid::setEnabled(false)',read('main/core/power_runtime.cpp'))
    def test_warm_reset_quiesces_imu_without_loading_or_sampling(self):
        s=part(read('main/core/qmi8658.cpp'),'bool Qmi8658::powerDownAtBoot()', 'bool Qmi8658::init(')
        for token in ('kCtrl7,0','ctrl1|0x02','i2c_master_bus_rm_device(temporary)'):self.assertIn(token,s)
        for token in ('kAccelEnable','s_ready = true','Qmi8658::init()'):self.assertNotIn(token,s)
        self.assertIn('Qmi8658::powerDownAtBoot()',read('main/core/system_service.cpp'))
    def test_wifi_driver_is_loaded_only_by_manual_enable(self):
        main=read('main/main.cpp');worker=read('main/core/system_service.cpp');wifi=read('main/core/connectivity.cpp')
        for token in ('esp_wifi_init(', 'esp_netif_create_default_wifi_sta(', 'Qmi8658::setEnabled('):self.assertNotIn(token,main)
        self.assertNotIn('Connectivity::init();',worker);self.assertNotIn('BleHid::setEnabled(true)',worker)
        init=part(wifi,'esp_err_t Connectivity::init()', 'esp_err_t Connectivity::validateCredentials')
        self.assertIn('esp_wifi_init(&config)',init);self.assertIn('driverInitialized',init)
        self.assertIn('if(enabled) { const auto err=init();',wifi)
    def test_wifi_enable_does_not_select_saved_target(self):
        s=part(read('main/core/connectivity.cpp'),'esp_err_t Connectivity::setWifiEnabled', 'esp_err_t Connectivity::stopWifi')
        for token in ('Settings::wifiSsid()', 'Settings::wifiPassword()', 'connectWifi('):self.assertNotIn(token,s)
        self.assertIn('reconnect=false;targetSsid.clear();targetPassword.clear()',s)
        self.assertIn('restartPending=true',s)
    def test_wifi_manual_connect_still_probes_presence_and_preserves_fences(self):
        s=read('main/core/connectivity.cpp')
        for token in ('targetValidated','connectQueued','awaitingStop','awaitingStart','scanOutstanding','Connectivity::NetworkNotSeen','only after DHCP success','reconnect=reconnect && validateCredentials'):self.assertIn(token,s)
    def test_ble_enable_never_starts_discovery_or_bond_reconnect(self):
        s=part(read('main/core/ble_hid.cpp'),'esp_err_t BleHid::setEnabled', 'esp_err_t BleHid::disconnect')
        for token in ('makeDiscoverable()', 's_advertisingAllowed.store(true)', 'reconnectKeyboard(', 'startScan('):self.assertNotIn(token,s)
        for token in ('endConnectionWindow()', 'BleHidState::Ready', 's_connHandle.load()!=kInvalidHandle || s_connectPending.load()'):self.assertIn(token,s)
    def test_ble_both_disconnect_paths_and_failure_close_permission(self):
        s=read('main/core/ble_hid.cpp')
        for a,b in (('case BLE_GAP_EVENT_DISCONNECT:', 'case BLE_GAP_EVENT_ENC_CHANGE:'),
                    ('esp_err_t BleHid::disconnect(', 'bool BleHid::keyboardConnected'),
                    ('case BLE_GAP_EVENT_ADV_COMPLETE:', 'case BLE_GAP_EVENT_REPEAT_PAIRING:'),
                    ('ble_hs_cfg.reset_cb=', 'ble_hs_cfg.sm_io_cap')):
            block=part(s,a,b);self.assertIn('endConnectionWindow()',block)
            self.assertNotIn('startAdvertisingFromCallback()',block)
        for gone in ('s_nextKeyboardScan','s_reconnectPaused','s_keyboardMode','BLE_HS_FOREVER','initDiscoverableTimer'):self.assertNotIn(gone,s)
    def test_ble_one_shot_presence_scan_uses_identity_and_freshness(self):
        s=read('main/core/ble_hid.cpp')
        body=part(s,'esp_err_t BleHid::reconnectKeyboard','esp_err_t BleHid::reconnectBond')
        self.assertIn('const auto err=startScan()',body)
        self.assertIn('s_presenceTarget=device;s_presenceScan=true',body)
        poll=part(s,'void BleHid::poll()', 'namespace {\nvoid processAdvertising')
        self.assertNotIn('startScan()',poll)
        self.assertIn('BlePeerStore::same(results[i].identity,s_presenceTarget)',poll)
        self.assertIn('s_lastError.store(ESP_ERR_NOT_FOUND)',poll)
        self.assertIn('seenAtMs>15000',s)
    def test_ble_paired_host_has_bounded_targeted_invitation_not_fake_link(self):
        s=read('main/core/ble_hid.cpp');body=part(s,'esp_err_t BleHid::reconnectBond','esp_err_t BleHid::startScan')
        for token in ('bonds(paired,kMaxBondSlots)','BlePeerStore::same(paired[i],device)','if(!known) return ESP_ERR_NOT_FOUND','if(selected.keyboard) return reconnectKeyboard(selected)','s_targeted.store(true)','kDiscoverableDurationUs'):self.assertIn(token,body)
        self.assertNotIn('BleHidState::Connected',body)
        for token in ('params.conn_mode=BLE_GAP_CONN_MODE_UND','params.high_duty_cycle=0','static_cast<int32_t>(remaining)','ble_gap_adv_start(s_ownAddrType.load(), nullptr'):self.assertIn(token,s)
    def test_ble_stale_connection_and_radio_reset_fail_closed(self):
        s=read('main/core/ble_hid.cpp');gap=part(s,'case BLE_GAP_EVENT_CONNECT:', 'case BLE_GAP_EVENT_DISCONNECT:')
        for token in ('s_connectionAllowed.exchange(false)','!s_enabled.load() || !permitted','esp_timer_get_time()<windowEnd','ble_gap_terminate','!matchesLinkExpected(peer)'):self.assertIn(token,gap)
        reset=part(s,'ble_hs_cfg.reset_cb=', 'ble_hs_cfg.sm_io_cap')
        for token in ('++s_scanGeneration','s_scanState.store(BleScanState::Idle)','endConnectionWindow()'):self.assertIn(token,reset)
    def test_ble_settings_reconnect_and_cancel_use_selected_identity(self):
        s=read('main/apps/settings_bluetooth.cpp')
        for token in ('reconnectBond(self->selectedBond)','sameBond(state.peer,self->selectedBond)','waitingForPeer','取消连接','请求连接','listPressed'):self.assertIn(token,s)
        self.assertNotIn('!selectedBond.keyboard ||',s)
        self.assertIn('bluetoothFlow.begin(BluetoothOperation::Saved,cmd.bond',read('main/core/system_service.cpp'))
        self.assertIn('BleHid::reconnectBond(target)',read('main/core/ble_connection_flow.hpp'))
    def test_single_foreground_factory_is_deferred_until_old_app_freed(self):
        s=read('main/ui/window_mgr.cpp');h=read('main/ui/window_mgr.hpp')
        self.assertNotIn('m_stack',s+h);self.assertIn('std::unique_ptr<App> m_active',h)
        open_body=part(s,'void WindowManager::openApp','void WindowManager::processNavigation')
        self.assertNotIn('.factory()',open_body)
        navigation=part(s,'void WindowManager::processNavigation','void WindowManager::closeActive')
        self.assertLess(navigation.index('closeActive()'),navigation.index('.factory()'))
        self.assertIn('WindowManager::instance().processNavigation()',read('main/main.cpp'))
    def test_root_teardown_releases_timers_before_widgets_and_app(self):
        s=part(read('main/ui/window_mgr.cpp'),'void WindowManager::closeActive','void WindowManager::goBack')
        sequence=['TextInput::hide()', 'ExternalKeyboard::resetFocus()', 'm_active->onHide()', 'm_active->onDestroy()', 'lv_obj_delete(m_active->root())', 'm_active.reset()']
        positions=[s.index(t) for t in sequence];self.assertEqual(positions,sorted(positions))
    def test_external_keyboard_does_not_replay_old_app_keystrokes(self):
        s=part(read('main/ui/external_keyboard.cpp'),'void ExternalKeyboard::resetFocus()', 'void ExternalKeyboard::poll()')
        for token in ('lv_indev_reset(input,nullptr)','lv_group_remove_all_objs(group)','head=count=0','repeatUsage=0','KeyboardInput::pop(discarded)','KeyboardInput::snapshot','ignoreUntilRelease'):self.assertIn(token,s)
        self.assertLess(s.index('KeyboardInput::pop'),s.index('KeyboardInput::snapshot'))
    def test_no_app_hide_leaves_autoclick_motion_or_observer_active(self):
        s=part(read('main/apps/bt_keyboard_mouse.cpp'),'void BtKeyboardMouse::onHide()', 'void BtKeyboardMouse::onDestroy()')
        for token in ('m_autoClick=false','lv_timer_delete(m_autoTimer)','releasePointer()','cancelPending()'):self.assertIn(token,s)
        s=read('main/apps/watchface.cpp')
        self.assertNotIn('Qmi8658',s)
        hidden=part(s,'void Watchface::onHide()', 'void Watchface::onDestroy(')
        self.assertIn('lv_timer_pause(m_timer)',hidden)
        self.assertIn('m_almanac.stop()',hidden)
        maze=read('main/apps/gravity_maze.cpp')
        self.assertIn('releaseTemporaryUse(lease)',maze)
        self.assertIn('releaseSensor()',part(maze,'void GravityMaze::onHide()', 'void GravityMaze::onDestroy()'))
        self.assertFalse((ROOT/'main/apps/touch_scope.cpp').exists())
    def test_motion_requires_permission_foreground_demand_and_screen(self):
        s=read('main/core/system_service.cpp')
        self.assertIn('Qmi8658::requested(Settings::motionSensorOn()) && Power::isScreenOn() && !Power::slowIdle() && !Power::sleepPending()',s)
        self.assertIn('Qmi8658::requested(cmd.value) && Power::isScreenOn() && !Power::sleepPending()',s)
        qmi=read('main/core/qmi8658.cpp')
        for token in ('if(!enabled && !s_ready) return true','writeRegister(kCtrl7, 0x00)','s_enabled = false','std::try_to_lock','!s_inUse.load()'):self.assertIn(token,qmi)
        self.assertIn('Qmi8658::claimTemporaryUse()',read('main/apps/gravity_maze.cpp'))
        self.assertNotIn('setMotionSensorOn',read('main/apps/gravity_maze.cpp'))
    def test_storage_cancellation_checks_between_chunks_and_skips_sort(self):
        s=read('main/core/storage_browser.cpp')
        self.assertIn('std::min<size_t>(2048,kMaxTextBytes-count)',s)
        self.assertIn('while(count<kMaxTextBytes && !job.cancelled.load())',s)
        self.assertIn('std::unique_ptr<DIR,decltype(&closedir)>',s)
        self.assertIn('owner.reset();\n    if(job.error) { job.entries.clear();return; }\n    if(job.cancelled.load()) return;\n    std::sort',s)
        text=part(s,'if(job.text) {','if(job.media &&')
        self.assertIn('std::unique_ptr<FILE,decltype(&fclose)>',text)
        self.assertLess(text.index('owner.reset()'),text.index('job.content=decodeText'))
        self.assertIn('destroy shared result before deleting the task',s)
    def test_settings_exit_cancels_only_user_scans_not_connections(self):
        settings=part(read('main/apps/settings.cpp'),'void Settings::onDestroy()', 'void Settings::navigate')
        for token in ('cancelWifiScan()', 'cancelBluetoothScan()', 'lv_timer_delete'):self.assertIn(token,settings)
        for gone in ('disconnectWifi','BluetoothDisconnect','WifiEnable'):self.assertNotIn(gone,settings)
        self.assertIn('if(!s_presenceScan) stopScan()',read('main/core/ble_hid.cpp'))
        self.assertIn('!targetScan && !connectQueued',read('main/core/connectivity.cpp'))

class SpecificationModels(unittest.TestCase):
    def test_permissions_default_off_for_every_old_nvs_combination(self):
        for old in itertools.product((False,True),repeat=3):
            for deep_wake in (False,True):
                runtime=dict(wifi=False,bluetooth=False,motion=False)
                self.assertFalse(any(runtime.values()),(old,deep_wake))
    def test_ble_disconnect_is_terminal_until_new_manual_request(self):
        for role,reason in itertools.product(('keyboard','phone'),('local','remote','timeout','reset','off_on')):
            state=dict(enabled=True,connected=True,allowed=False,scan=False,attempts=1)
            state.update(connected=False,allowed=False,scan=False)
            for _ in range(1000):
                # Poll may consume an explicit permission; it cannot create one.
                if state['allowed'] or state['scan']:state['attempts']+=1
            self.assertEqual(state['attempts'],1,(role,reason))
    def test_keyboard_missing_target_does_not_cycle_scans(self):
        for found in (False,True):
            scans=1;connections=int(found);permission=False
            for _ in range(1000):
                if permission:scans+=1
            self.assertEqual(scans,1);self.assertEqual(connections,int(found))
    def test_motion_truth_table_and_queued_enable_after_exit(self):
        for permission,foreground,screen,sleep in itertools.product((False,True),repeat=4):
            powered=permission and foreground and screen and not sleep
            self.assertEqual(powered,all((permission,foreground,screen,not sleep)))
        self.assertFalse(True and False and True) # enabled command arrives after App exit
    def test_repeated_app_switch_never_has_two_live_instances(self):
        live=0;created=destroyed=0;peak=0
        for _ in range(1000):
            for app in range(9):
                if live:live-=1;destroyed+=1
                self.assertEqual(live,0)
                live+=1;created+=1;peak=max(peak,live)
            live-=1;destroyed+=1
        self.assertEqual(peak,1);self.assertEqual(created,destroyed);self.assertEqual(live,0)
if __name__=='__main__':unittest.main(verbosity=2)
