"""Guard manual iPhone invitations and the accepted Wi-Fi memory fixes."""
from pathlib import Path
import unittest

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def section(s,start,end):return s.split(start,1)[1].split(end,1)[0]

class IphoneContracts(unittest.TestCase):
    def test_security_request_is_delayed_only_for_peripheral(self):
        s=read('main/core/ble_hid.cpp')
        connect=section(s,'case BLE_GAP_EVENT_CONNECT:', 'case BLE_GAP_EVENT_DISCONNECT:')
        self.assertIn('if(s_keyboardLink.load())',connect)
        self.assertIn('ble_npl_time_ms_to_ticks32(1000)',connect)
        fallback=section(s,'void securityFallback(', 'int gapEvent(')
        for token in ('s_securityLink!=s_linkGeneration.load()', 's_disconnectPending.load()',
                      'desc.sec_state.encrypted', 'rc!=BLE_HS_ENOTCONN'):
            self.assertIn(token,fallback)
        for name in ('sdkconfig','sdkconfig.defaults'):
            self.assertIn('CONFIG_BT_NIMBLE_SM_LVL=2',read(name))

    def test_smp_teardown_preserves_disconnect_reason_and_bounded_invitation(self):
        s=read('main/core/ble_hid.cpp')
        enc=section(s,'case BLE_GAP_EVENT_ENC_CHANGE:', 'case BLE_GAP_EVENT_NOTIFY_RX:')
        self.assertLess(enc.index('status==BLE_HS_ENOTCONN)return 0'),enc.index('clearPairRequest()'))
        disconnect=section(s,'case BLE_GAP_EVENT_DISCONNECT:', 'case BLE_GAP_EVENT_ENC_CHANGE:')
        for token in ('!cancelled', '!s_keyboardLink.load()', 'remaining>0', '!s_pairError.load()',
                      'BLE_HS_HCI_ERR(BLE_ERR_REM_USER_CONN_TERM)', 's_pairError.store(event->disconnect.reason)',
                      'BlePeerStore::activate(expected)', 'ble_npl_callout_stop'):
            self.assertIn(token,disconnect)
        self.assertNotIn('s_discoverableDeadlineUs.store(',disconnect)
        self.assertNotIn('ble_gap_connect(',disconnect)

    def test_sdk_cannot_retry_outside_manual_connection_lifecycle(self):
        for path in ('sdkconfig','sdkconfig.defaults'):
            s=read(path)
            self.assertIn('# CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT is not set',s)
            self.assertNotIn('\nCONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT=y',s)
        self.assertIn('#error "BLE automatic reattempt must be disabled',read('main/core/ble_hid.cpp'))

    def test_saved_peer_is_filtered_before_occupying_only_connection(self):
        s=section(read('main/core/ble_hid.cpp'),'namespace {\nvoid processAdvertising()', '} // namespace\nesp_err_t BleHid::prepareDeepSleep')
        for item in ('ble::resolveIdentity(address,identity,ble_gap_rpa_resolve)',
                     'ble_gap_wl_set(&identity,1)',
                     'params.filter_policy=targeted?BLE_HCI_ADV_FILT_CONN:BLE_HCI_ADV_FILT_NONE',
                     'endConnectionWindow()', 'static_cast<int32_t>(remaining)'):
            self.assertIn(item,s)
        self.assertLess(s.index('ble_gap_wl_set'),s.index('ble_gap_adv_start'))
        self.assertNotIn('ble_gap_connect(',s)
        self.assertNotIn('BLE_GAP_CONN_MODE_DIR',s)

    def test_hid_appearance_and_discovery_packets_are_consistent(self):
        s=read('main/core/ble_hid.cpp')
        config=section(s,'esp_err_t configureGap(', 'void hiddEvent(')
        self.assertIn('s_advFields.appearance = ESP_HID_APPEARANCE_KEYBOARD',config)
        self.assertIn('s_advFields.uuids16 = &s_hidUuid',config)
        self.assertNotIn('TX_PWR_LVL_AUTO',config)
        init=section(s,'esp_err_t BleHid::init(', 'esp_err_t BleHid::setEnabled(')
        self.assertLess(init.index('ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_KEYBOARD)'),init.index('xTaskCreateStaticPinnedToCore'))
        self.assertIn('ble_gap_adv_rsp_set_fields(&response)',s)
        self.assertIn('response.name_len=sizeof(kDeviceName)-1;response.name_is_complete=1',s)

    def test_scan_ui_explains_phone_role_and_shows_real_state(self):
        s=read('main/apps/settings_bluetooth.cpp')
        self.assertIn('case sys::BleHidState::Advertising:return',s)
        self.assertIn('再在对方蓝牙设置中选择本机',s)
        detail=section(s,'} else if(page==Page::BluetoothDevice)', '} else if(page==Page::BluetoothScan)')
        self.assertIn('连接异常 rc=%d',detail)

if __name__=='__main__':unittest.main(verbosity=2)
