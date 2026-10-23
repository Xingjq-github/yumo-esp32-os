"""Apple reconnect privacy and bounded invitation source contracts.

No radio, serial, real bond store or service access. Compile-time production
address tests are in ble_peer_identity_test.cpp; real phone testing is separate.
"""
from pathlib import Path
import unittest

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf8')
def part(text,start,end):return text.split(start,1)[1].split(end,1)[0]

class BlePeerIdentity(unittest.TestCase):
    def test_selected_identity_is_held_until_encryption_and_role_checked(self):
        s=read('main/core/ble_hid.cpp')
        connect=part(s,'case BLE_GAP_EVENT_CONNECT:', 'case BLE_GAP_EVENT_DISCONNECT:')
        self.assertLess(connect.index('s_linkExpected=BlePeerStore::active()'),connect.index('connectionPeer(peerDesc,peer,true)'))
        self.assertLess(connect.index('!matchesLinkExpected(peer)'),connect.index('ble_gap_security_initiate'))
        self.assertIn('if(permitted)BlePeerStore::activate(peer)',connect)
        match=part(s,'bool matchesLinkExpected(', 'int scanEvent(')
        self.assertIn('BlePeerStore::same(expected,peer) && expected.keyboard==peer.keyboard',match)
        self.assertIn('if(!s_verifyExpected)return true',match)
        self.assertIn('ble::resolveIdentity(address,identity,ble_gap_rpa_resolve)',match)
        self.assertIn('completeSecureConnection(event->connect.conn_handle)',connect)
        self.assertIn('completeSecureConnection(event->enc_change.conn_handle)',part(s,'case BLE_GAP_EVENT_ENC_CHANGE:', 'case BLE_GAP_EVENT_NOTIFY_RX:'))
        enc=part(s,'bool completeSecureConnection(', 'void securityFallback(')
        for token in ('connectionPeer(desc,peer)','!matchesLinkExpected(peer)','s_disconnectPending.store(true)',
                      's_privateEligible.store(false)','BLE_HS_EAUTHEN','ble_gap_terminate'):
            self.assertIn(token,enc)
        self.assertLess(enc.index('!matchesLinkExpected(peer)'),enc.index('BlePeerStore::remember(peer)'))
        self.assertLess(enc.index('!matchesLinkExpected(peer)'),enc.index('s_privateEligible.store(desc.sec_state.encrypted'))
        self.assertLess(enc.index('!matchesLinkExpected(peer)'),enc.index('BleHidState::Connected'))

    def test_scan_connect_and_saved_status_share_sdk_irk_resolver(self):
        s=read('main/core/ble_hid.cpp');helper=part(s,'bool connectionPeer(', 'bool matchesLinkExpected(')
        for token in ('desc.peer_id_addr,identity,ble_gap_rpa_resolve','desc.peer_ota_addr,identity,ble_gap_rpa_resolve',
                      'peer.addrType=identity.type','BlePeerStore::enrich(peer)'):
            self.assertIn(token,helper)
        self.assertIn('if(!allowUnresolved',helper) # new, unpaired phone has not yet shared an IRK
        self.assertIn('ble::resolveIdentity(disc.addr,resolved,ble_gap_rpa_resolve)',s)
        bonds=part(s,'int BleHid::bonds(', 'esp_err_t BleHid::removeBond(')
        self.assertIn('haveActive = connectionPeer(desc,active)',bonds)
        self.assertIn('connected() && haveActive && BlePeerStore::same(active,out[i])',bonds)
        h=read('main/core/ble_peer_identity.hpp')
        for token in ('input.type>3','input.type==1 && (input.val[5]&0xc0)==0x40','!resolver(input.val,resolved,&type)',
                      'if(!nonzero)return false','identity.type=type&1'):
            self.assertIn(token,h)
        self.assertNotRegex(helper,r'ble_gap_unpair|delete_peer|nvs_erase')

    def test_apple_advertising_is_exact_discoverable_and_only_manual(self):
        s=read('main/core/ble_hid.cpp');adv=part(s,'namespace {\nvoid processAdvertising()', '} // namespace\nesp_err_t BleHid::prepareDeepSleep()')
        for token in ('s_discoverable.load() || targeted','BLE_HS_ADV_F_DISC_GEN','params.conn_mode=BLE_GAP_CONN_MODE_UND',
                      'params.itvl_min=ble::ManualAdvertisingInterval','params.itvl_max=ble::ManualAdvertisingInterval',
                      'static_cast<int32_t>(remaining)','!s_connectionAllowed.load()','endConnectionWindow()'):
            self.assertIn(token,adv)
        self.assertIn('ManualAdvertisingInterval=32',read('main/core/ble_peer_identity.hpp'))
        self.assertIn('kDiscoverableDurationUs = 60LL * 1000 * 1000',s)
        for start,end in [('esp_err_t BleHid::disconnect(', 'bool BleHid::keyboardConnected('),
                          ('case BLE_GAP_EVENT_DISCONNECT:', 'case BLE_GAP_EVENT_ENC_CHANGE:'),
                          ('esp_err_t BleHid::makeDiscoverable(', 'esp_err_t BleHid::stopDiscoverable(')]:
            body=part(s,start,end)
            self.assertNotRegex(body,r'ble_gap_connect\(|reconnectBond\(|reconnectKeyboard\(')
            self.assertIn('endConnectionWindow()',body)
        poll=part(s,'void BleHid::poll()', 'namespace {\nvoid processAdvertising()')
        self.assertIn('if(!restart && !requested) return',poll)
        self.assertIn('windowEnd && now>=windowEnd',poll)
        self.assertIn('s_targeted.store(false)',part(s,'esp_err_t BleHid::makeDiscoverable(', 'esp_err_t BleHid::stopDiscoverable('))

    def test_all_user_interfaces_keep_shared_manual_connection_api(self):
        service=read('main/core/system_service.cpp')
        self.assertIn('bluetoothFlow.begin(BluetoothOperation::Saved,cmd.bond',service)
        self.assertIn('BleHid::reconnectBond(target)',read('main/core/ble_connection_flow.hpp'))
        self.assertIn('connectionGeneration.load()',service)
        self.assertIn('next.waitingForPeer=BleHid::waitingForPeer()',service)
        for file in ('main/apps/settings_bluetooth.cpp','main/apps/password_vault.cpp','main/apps/fc_player.cpp'):
            self.assertIn('SystemService::reconnectBond',read(file))
        self.assertIn('ble_peer_identity_test.cpp',[p.name for p in (ROOT/'../tooling/firmware').glob('*_test.cpp')])

if __name__=='__main__':unittest.main(verbosity=2)
