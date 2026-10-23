"""Contracts for the keyboard/battery/UI revision. Read-only, no hardware access.
Production HID and estimator behavior also has a separate compile-time C++ test.
"""
from pathlib import Path
import re,unittest
from check_battery_power import Model,feed
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BASE=ROOT/'../tooling/firmware/fixtures/contracts/keyboard-battery-ui-20260917-1306'
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def validate_config(test):
    values=lambda s:dict(re.findall(r'^(CONFIG_\w+)=(.*)$',s,re.M))
    before,after=values((BASE/'sdkconfig').read_text(encoding='utf-8')),values(read('sdkconfig'))
    changed={k:after.get(k) for k in before.keys()|after.keys() if before.get(k)!=after.get(k)}
    test.assertEqual(changed,{'CONFIG_BT_NIMBLE_ROLE_CENTRAL':'y','CONFIG_NIMBLE_ROLE_CENTRAL':'y','CONFIG_BT_NIMBLE_GATT_CLIENT':'y'})
    test.assertEqual(after['CONFIG_BT_NIMBLE_MAX_CONNECTIONS'],'1')
    old=(BASE/'sdkconfig.defaults').read_text(encoding='utf-8')
    test.assertEqual(read('sdkconfig.defaults'),old.replace('CONFIG_BT_NIMBLE_ROLE_CENTRAL=n','CONFIG_BT_NIMBLE_ROLE_CENTRAL=y'))
    for n in ('dependencies.lock','partitions.csv','main/idf_component.yml'):
        test.assertEqual((ROOT/n).read_bytes(),(BASE/n).read_bytes(),n)
class KeyboardBatteryContracts(unittest.TestCase):
    def test_only_authorized_configuration_change(self):validate_config(self)
    def test_cmake_includes_all_new_implementations_once(self):
        src=re.findall(r'"([^"\n]+\.(?:c|cpp))"',read('main/CMakeLists.txt'))
        self.assertEqual(len(src),48);self.assertEqual(len(set(src)),48)
        for n in ('core/ble_keyboard_client.cpp','core/keyboard_input.cpp','core/ble_peer_store.cpp','ui/external_keyboard.cpp','ui/bluetooth_pairing.cpp'):self.assertIn(n,src)
    def test_single_connection_and_role_gates(self):
        s=read('main/core/ble_hid.cpp')
        for text in ('s_connHandle.load()!=kInvalidHandle || s_connectPending.load()', 'peerDesc.role==BLE_GAP_ROLE_MASTER', 'ble_gap_adv_stop()', 'BleKeyboardClient::keyboardReady()', 'BleKeyboardClient::gamepadReady()', 'remoteConnected()', 's_disconnectPending.load()'):self.assertIn(text,s)
        for n in ('main/apps/bt_keyboard_mouse.cpp','main/apps/bt_music.cpp'):self.assertIn('BleHid::remoteConnected()',read(n))
    def test_reconnect_presence_identity_and_freshness(self):
        s=read('main/core/ble_hid.cpp')
        for text in ('ble::resolveIdentity(disc.addr,resolved,ble_gap_rpa_resolve)', 'BlePeerStore::same(results[i].identity,s_presenceTarget)', 'seenAtMs>15000', 'params.passive = 0', 'endConnectionWindow()', 's_presenceScan=false'):self.assertIn(text,s)
        self.assertNotIn('strcmp(results[i].name',s)
    def test_keyboard_protocol_and_bounded_discovery(self):
        s=read('main/core/ble_keyboard_client.cpp')
        for text in ('0x1812','0x2a4b','0x2a4d','0x2908','0x2902','0x2a22','map.parse','map.decode','generation','charCount==workspace->chars.size()','inputCount==workspace->inputs.size()'):self.assertIn(text,s)
        self.assertNotIn('portMAX_DELAY',s);self.assertNotIn('lv_',s)
        self.assertIn('s_securityDeadlineUs.store(esp_timer_get_time()+20000000)',read('main/core/ble_hid.cpp'))
    def test_large_keyboard_buffers_do_not_consume_internal_static_ram(self):
        for path in ('main/core/ble_keyboard_client.cpp','main/ui/external_keyboard.cpp'):
            source=read(path)
            self.assertIn('Workspace *workspace=nullptr;',source)
            self.assertIn('heap_caps_malloc(sizeof(Workspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)',source)
            self.assertIn('if(!memory)',source)
            self.assertIn('workspace=new(memory) Workspace{};',source)
        source=read('main/core/ble_hid.cpp')
        self.assertIn('BleScanDevice *s_scanDevices=nullptr;',source)
        self.assertIn('sizeof(BleScanDevice)*kMaxScanDevices,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',source)
        self.assertIn('if(count>0) std::memcpy(out, s_scanDevices',source)
        self.assertIn('StaticTask_t s_hostTaskControl;',source)
        self.assertIn('portMUX_TYPE s_scanLock',source)
        source=read('main/core/system_service.cpp')
        self.assertIn('StaticQueue_t control;',source)
        self.assertIn('8*sizeof(Command),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',source)
        self.assertIn('if(!storage) return false;',source)
        source=read('main/core/keyboard_input.cpp')
        self.assertIn('InputQueue *queue=nullptr;',source)
        self.assertIn('sizeof(InputQueue),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',source)
        self.assertIn('count=queue?1:0',source)
        self.assertIn('if(!KeyboardInput::init())',read('main/core/ble_hid.cpp'))
    def test_reports_and_disconnect_release_keys(self):
        s=read('main/core/keyboard_input.cpp')
        for token in ('queue->size()-1','(*queue)[0]={}','portENTER_CRITICAL','state.bits!=keys.bits'):self.assertIn(token,s)
        self.assertIn('BleKeyboardClient::reset();BlePeerStore::clearActive()',read('main/core/ble_hid.cpp'))
    def test_keypad_focus_wake_and_hidden_software_input(self):
        s=read('main/ui/external_keyboard.cpp')
        for token in ('LV_INDEV_TYPE_KEYPAD','lv_obj_is_hidden','LV_STATE_DISABLED','ignoreUntilRelease','LV_KEY_NEXT','LV_KEY_PREV','lv_group_remove_all_objs','BluetoothPairing::root()','lv_group_get_obj_count(group)==size','if(command) repeatUsage=0'):self.assertIn(token,s)
        self.assertIn('BleHid::keyboardConnected() && !sys::BleKeyboardClient::iineKeyboard()) { hide();return; }',read('main/ui/text_input.cpp'))
    def test_global_pairing_owns_overlay_and_generation(self):
        s=read('main/ui/bluetooth_pairing.cpp')
        for token in ('lv_layer_top()','state.pairGeneration==answered','answerPairing(accept,generation)','identity.update(state.peer)','lv_font_montserrat_24','sys::Power::sleepPending()'):self.assertIn(token,s)
        self.assertNotIn('Pairing passkey to enter',read('main/core/ble_hid.cpp'))
        self.assertIn('BluetoothPairing::poll()',read('main/main.cpp'))
        self.assertNotIn('refreshPairing',read('main/apps/settings.cpp'))
    def test_names_cached_without_replacing_security_keys(self):
        s=read('main/core/ble_peer_store.cpp')
        self.assertIn('nvs_open("ble_peers"',s);self.assertIn('NVS_READWRITE',s)
        self.assertNotIn('nvs_erase_all',s);self.assertNotIn('ble_store',s)
        self.assertIn('if(same(peer,target())) setTarget(peer)',s)
        self.assertIn('BlePeerStore::flush()',read('main/core/ble_hid.cpp').split('esp_err_t BleHid::prepareDeepSleep()',1)[1])
    def test_all_device_views_use_two_line_identity(self):
        s=read('main/ui/bluetooth_identity.hpp')
        self.assertIn('y+20',s);self.assertIn('lv_font_montserrat_10',s)
        for n in ('settings_bluetooth.cpp',):
            self.assertRegex(read('main/apps/'+n),r'(?:[Ii]dentity|peerIdentity)\.(?:create|update)')
        self.assertIn('identity.update(selectedBond)',read('main/apps/settings.cpp'))
        for n in ('bt_keyboard_mouse.cpp','bt_music.cpp'):
            self.assertNotIn('m_identity',read('main/apps/'+n))
    def test_battery_range_and_no_hardware_charge_override(self):
        s=read('main/core/battery_mgr.cpp')
        self.assertIn('kAtten=ADC_ATTEN_DB_6',s);self.assertIn('adc_cali_create_scheme_curve_fitting',s)
        self.assertIn('kDividerRatio=3',s);self.assertIn('gpio_get_level(kChargePin)==0',s)
        self.assertNotIn('gpio_set_level',s)
        self.assertIn('esp_hidd_dev_battery_set',read('main/core/ble_hid.cpp'))
        self.assertIn('publishBattery(now)',read('main/core/ble_hid.cpp'))
    def test_battery_model_one_point_change_still_converges(self):
        m=Model();feed(m,0,60000,4000,True);self.assertEqual(m.percent,85)
        feed(m,61000,1200000,4006,True);self.assertEqual(m.percent,86)
    def test_battery_model_full_is_qualified_not_forced(self):
        m=Model();feed(m,0,300000,4110,True);self.assertEqual(m.percent,99)
        feed(m,301000,1200000,4110,False);self.assertEqual(m.percent,100)
        low=Model();feed(low,0,1200000,4090,False);self.assertEqual(low.percent,98)
        below=Model();feed(below,0,1200000,4109,False);self.assertEqual(below.percent,99)
        cold=Model();feed(cold,0,1200000,4110,False);self.assertEqual(cold.percent,100)
    def test_clock_gesture_only_and_content_fill_screen(self):
        s=read('main/apps/watchface.cpp')
        for token in ('kFaceHeight=240','kDialSize = kFaceHeight','panel(parent,240,kFaceHeight)','lv_obj_set_style_border_width(m_dial,0,0)','changeFace(dx < 0 ? 1 : -1)'):
            self.assertIn(token,s)
        for token in ('kSelectorHeight','m_dots','auto selector','::button('):self.assertNotIn(token,s)
    def test_torch_third_level_and_sos_preserved(self):
        s=read('main/apps/flashlight.cpp');h=read('main/apps/flashlight_signal.hpp')
        self.assertIn('m_level=2',s);self.assertIn('setSos(false)',s)
        from check_auto_power import original_sos
        old=(BASE/'main/apps/flashlight_signal.hpp').read_text(encoding='utf-8');self.assertEqual(old,original_sos(h))
        for token in ('ScreenOff','setSos'):
            self.assertIn(token,s)
    def test_core_functionality_not_unintentionally_changed(self):
        from check_netdisk_cleanup import verify_power_and_buttons_preserved
        verify_power_and_buttons_preserved(self)
        for n in ('core/charset_conv.cpp','apps/settings_wifi.cpp','apps/text_reader.cpp','ui/home_screen.cpp','ui/boot_animation.cpp'):
            self.assertEqual((ROOT/'main'/n).read_bytes(),(BASE/'main'/n).read_bytes(),n)
