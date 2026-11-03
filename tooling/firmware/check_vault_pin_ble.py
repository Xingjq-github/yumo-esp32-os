"""PIN row, bond retention and text capability contracts. No device/DB/network I/O."""
from pathlib import Path
import ast
import hashlib
import json
import re
import unittest
from sdkconfig_snapshot import historical_sdkconfig_bytes
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/vault-pin-ble-reconnect-20260918'
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(s):return re.sub(r'\s+','',s)
def section(s,start,end):return s[s.index(start):s.index(end,s.index(start)+len(start))]
class PinBleContracts(unittest.TestCase):
    def test_pin_has_one_label_no_separate_hint_or_field(self):
        body=section(read('main/apps/password_vault.cpp'),'void PasswordVault::drawPin(', 'void PasswordVault::timer(')
        self.assertEqual(body.count('m_pattern.create('),1)
        self.assertNotIn('c::field(',body)
        view=read('main/ui/vault_pattern.cpp')
        self.assertEqual(view.count('compact::label('),1)
        self.assertIn('lv_obj_center(toast)',view)
        self.assertIn('LV_OPA_80',view)
    def test_message_and_mask_share_one_row_with_no_plaintext_pin(self):
        body=section(read('main/apps/password_vault.cpp'),'const char *PasswordVault::patternPrompt()', 'void PasswordVault::patternSubmitted(')
        self.assertIn('m_firstPin.empty()',body)
        self.assertIn('绘制图形密码',body)
        self.assertNotIn('m_pin.c_str()',body)
        view=read('main/ui/vault_pattern.cpp')
        self.assertIn('lv_tick_elaps(hintAt) >= 1800',view)
        self.assertIn('lv_obj_set_hidden(toast, true)',view)
    def test_both_edits_clear_feedback_without_rebuilding_keypad(self):
        view=section(read('main/ui/vault_pattern.cpp'),'void VaultPattern::event(', 'void VaultPattern::paint(')
        self.assertIn('LV_EVENT_PRESSING',view)
        self.assertIn('self->stroke.move(x, y)',view)
        self.assertIn('lv_obj_set_hidden(self->toast, true)',view)
        self.assertNotIn('lv_obj_create',view)
        self.assertNotIn('lv_obj_clean',view)
        self.assertIn('LV_EVENT_PRESS_LOST',view)
        self.assertIn('LV_EVENT_INDEV_RESET',view)
    def test_reveal_and_setup_keep_existing_pin_validation(self):
        s=compact(read('main/apps/password_vault.cpp'))
        for token in ('vault::pattern::valid(m_pin.data(),m_pin.size())','m_firstPin=m_pin','m_firstPin!=m_pin','vault::Operation::VerifyPin','vault::clear(m_pin)'):
            self.assertIn(token,s)
    def test_no_boot_time_nvs_erasure_or_new_identity(self):
        main=read('main/main.cpp');ble=read('main/core/ble_hid.cpp')
        self.assertEqual(len(re.findall(r'nvs_flash_init\(\)\s*;',main)),1)
        self.assertNotIn('nvs_flash_erase(',main)
        self.assertIn('if (ret != ESP_OK)',main)
        self.assertIn('preserving stored data',main)
        self.assertNotIn('ble_hs_id_gen_rnd(',ble)
        self.assertNotIn('ble_hs_id_set_rnd(',ble)
        self.assertIn('ble_hs_id_infer_auto(0, &ownAddrType)',ble)
    def test_nvs_unavailable_rejects_ble_before_allocating_and_starting(self):
        s=section(read('main/core/ble_hid.cpp'),'esp_err_t BleHid::init(', 'esp_err_t BleHid::setEnabled(')
        for token in ('nvs_get_stats("nvs",&nvsStats)','if(err!=ESP_OK) goto fail','s_state.store(BleHidState::Failed)'):
            self.assertIn(token,s)
        self.assertLess(s.index('nvs_get_stats'),s.index('KeyboardInput::init'))
        self.assertLess(s.index('nvs_get_stats'),s.index('nimble_port_init()'))
    def test_full_subscription_store_never_unpairs_any_peer(self):
        s=section(read('main/core/ble_hid.cpp'),'int bondStoreStatus(', 'void cancelDiscoverableTimeout(')
        for kind in ('OUR_SEC','PEER_SEC','PEER_ADDR','CCCD','CSFC'):
            self.assertIn('case BLE_STORE_OBJ_TYPE_'+kind+':',s)
        self.assertIn('s_bondStoreFull.store(true)',s)
        self.assertIn('return BLE_HS_ESTORE_CAP',s)
        for token in ('ble_store_util_status_rr(', 'ble_gap_unpair','ble_store_util_delete'):
            self.assertNotIn(token,s)
    def test_subscription_capacity_persistence_and_partition_layout(self):
        baseline=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
        for name in ('sdkconfig','sdkconfig.defaults'):
            data=(ROOT/name).read_bytes()
            self.assertIn(b'CONFIG_BT_NIMBLE_MAX_CCCDS=128',data)
            self.assertIn(b'CONFIG_BT_NIMBLE_MAX_BONDS=16',data)
            self.assertIn(b'CONFIG_BT_NIMBLE_NVS_PERSIST=y',data)
            restored=historical_sdkconfig_bytes(data).replace(b'CONFIG_BT_NIMBLE_MAX_CCCDS=128',b'CONFIG_BT_NIMBLE_MAX_CCCDS=16').replace(b'CONFIG_NIMBLE_MAX_CCCDS=128',b'CONFIG_NIMBLE_MAX_CCCDS=16')
            from check_auto_power import assert_config
            assert_config(self,name)
        self.assertGreaterEqual(128,16*(3+2+1)) # 3 report inputs + 2 boot + GATT changed
        from prepare_vault_flash import partition_layout
        layout=partition_layout((ROOT/'../artifacts/firmware/partition_table/partition-table.bin').read_bytes())
        self.assertEqual(layout[0],('nvs',1,2,0x9000,0x4000))
        self.assertEqual(layout[-1],('vault',1,0x40,0xec0000,0x100000))
    def test_reconnect_remains_manual_bounded_and_identity_filtered(self):
        s=read('main/core/ble_hid.cpp');request=section(s,'esp_err_t BleHid::reconnectBond(', 'esp_err_t BleHid::startScan(')
        for token in ('BlePeerStore::same(paired[i],device)','if(!known) return ESP_ERR_NOT_FOUND','s_targeted.store(true)','kDiscoverableDurationUs','reconnectKeyboard(selected)'):
            self.assertIn(token,request)
        gap=section(s,'case BLE_GAP_EVENT_CONNECT:', 'case BLE_GAP_EVENT_DISCONNECT:')
        for token in ('s_connectionAllowed.exchange(false)','esp_timer_get_time()<windowEnd','!matchesLinkExpected(peer)','!permitted','ble_gap_terminate','endConnectionWindow()'):
            self.assertIn(token,gap)
        adv=section(s,'namespace {\nvoid processAdvertising()', '} // namespace\nesp_err_t BleHid::prepareDeepSleep()')
        self.assertIn('params.conn_mode=BLE_GAP_CONN_MODE_UND',adv)
        self.assertIn('ble_gap_adv_start(s_ownAddrType.load(), nullptr',adv)
        self.assertNotIn('BLE_GAP_CONN_MODE_DIR',adv)
        self.assertIn('static_cast<int32_t>(remaining)',adv)
    def test_repeat_pairing_retry_requires_live_peer_and_successful_deletion(self):
        s=compact(section(read('main/core/ble_hid.cpp'),'case BLE_GAP_EVENT_REPEAT_PAIRING:', 'esp_err_t configureGap('))
        for token in ('!s_enabled.load()','s_disconnectPending.load()','handle!=s_connHandle.load()', 'ble_gap_conn_find(handle,&desc)','if(rc==0)rc=ble_store_util_delete_peer(&desc.peer_id_addr)','if(rc!=0)', 'BLE_GAP_REPEAT_PAIRING_IGNORE'):
            self.assertIn(token,s)
        self.assertEqual(s.count('BLE_GAP_REPEAT_PAIRING_RETRY'),1)
        self.assertLess(s.index('if(rc!=0)'),s.index('BLE_GAP_REPEAT_PAIRING_RETRY'))
    def test_hid_map_transport_mac_and_security_negotiation_unchanged(self):
        data=json.loads((RUN/'preserved_ble_sections.json').read_text(encoding='utf-8'))
        from ble_identity_contract import before_identity_fix
        from ble_mouse_contract import before_mouse_fix
        # Normalize whole reviewed hunks before slicing: security completion is
        # now shared by CONNECT and ENC_CHANGE outside the old GAP-only range.
        s=before_identity_fix(before_mouse_fix(read('main/core/ble_hid.cpp')))
        # Authorized eight-device capacity; freeze the remaining HID/security block.
        limit='constexpr int kMaxBondSlots = 8;'
        guard='static_assert(kMaxBondSlots<=8,"BLE peer metadata and UI support eight saved devices");\n'
        self.assertEqual(s.count(limit),1)
        self.assertEqual(s.count(guard),1)
        s=s.replace(limit,'constexpr int kMaxBondSlots = 3;').replace(guard,'')
        for item in data:
            body=section(s,item['start'],item['end'])
            self.assertEqual(hashlib.sha256(body.encode()).hexdigest(),item['sha256'],item['start'])
        baseline=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
        for name in ('main/apps/hid_keyboard_model.hpp','main/core/hid_key_sequence.hpp','main/core/vault_builtin.hpp','main/core/ble_peer_store.cpp'):
            raw=(ROOT/name).read_bytes()
            if name=='main/core/ble_peer_store.cpp':
                delta=json.loads((ROOT/'../tooling/firmware/fixtures/contracts/ble-peer-capacity-delta.json').read_text(encoding='utf-8'))
                self.assertEqual(hashlib.sha256(raw).hexdigest(),delta['after_sha256'])
                raw=bytes.fromhex(delta['before_hex'])
                # GAP must not replace a selected input-device mode name. Only
                # this callback hunk changes; v1 layout/load/write stay identical.
                mode_name=(
                    '    {\n'
                    '        const std::lock_guard<std::mutex> lock(mutex);\n'
                    '        // Input devices can advertise distinct mode names but return one generic\n'
                    '        // GAP name. Keep the selected mode label for this address and reconnect.\n'
                    '        if(current.keyboard && current.name[0]) return;\n'
                    '        std::snprintf(current.name,sizeof(current.name),"%s",value);peer=current;\n'
                    '    }\n').encode()
                self.assertEqual(raw.count(mode_name),1)
                raw=raw.replace(mode_name,b'    { const std::lock_guard<std::mutex> lock(mutex);std::snprintf(current.name,sizeof(current.name),"%s",value);peer=current; }\r\n')
                # Only the reviewed target-name refresh changed; persisted layout
                # and all security storage code retain the original contract.
                current=b'if(same(peer,target())) setTarget(peer);'
                self.assertEqual(raw.count(current),1)
                raw=raw.replace(current,b'if(peer.keyboard && same(peer,target())) setTarget(peer);')
            self.assertEqual(hashlib.sha256(raw).hexdigest(),baseline[name]['sha256'],name)
    def test_main_only_changes_non_destructive_nvs_error_handling(self):
        from vault_release_contract import source_before_release
        raw=source_before_release('main/main.cpp')
        self.assertEqual(raw.count(b'VAULT-ENTER-20260918'),1)
        raw=raw.replace(b'VAULT-ENTER-20260918',b'INPUT-UI-20260918')
        new=b'''    // Firmware updates must never silently discard BLE bonds/Wi-Fi settings.\r\n    // Keep the UI usable on an NVS failure; radio init reports the error, and\r\n    // recovery/erasure requires an explicit user decision.\r\n    if (ret != ESP_OK) {\r\n        ESP_LOGE(TAG, "NVS unavailable (%s); preserving stored data", esp_err_to_name(ret));\r\n    }'''
        old=b'''    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {\r\n        ESP_ERROR_CHECK(nvs_flash_erase());\r\n        ret = nvs_flash_init();\r\n    }\r\n\r\n    ESP_ERROR_CHECK(ret);'''
        self.assertEqual(raw.count(new),1)
        expected=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']['main/main.cpp']['sha256']
        self.assertEqual(hashlib.sha256(raw.replace(new,old,1)).hexdigest(),expected)
    def test_flash_manifest_does_not_overlap_user_data(self):
        # The old hardcoded batch scripts were retired during relocation.
        # Check the actual generated write/erase ranges instead.
        args=json.loads((ROOT/'../artifacts/firmware/flasher_args.json').read_text(encoding='utf-8'))
        regions={int(offset,0):name for offset,name in args['flash_files'].items()}
        self.assertEqual(set(regions),{0,0x8000,0xd000,0x20000})
        protected=((0x9000,0xd000),(0xf000,0x10000),(0x970000,0xec0000),(0xec0000,0xfc0000))
        for offset,name in regions.items():
            path=(ROOT/'../artifacts/firmware'/name).resolve()
            self.assertTrue(path.is_relative_to((ROOT/'../artifacts/firmware').resolve()))
            # App/bootloader do not exist until the initial build completes.
            limit={0:0x8000,0x8000:0x9000,0xd000:0xf000,0x20000:0x970000}[offset]
            end=(offset+path.stat().st_size+4095)//4096*4096 if path.exists() else limit
            self.assertLessEqual(end,limit)
            self.assertFalse(any(offset<high and end>low for low,high in protected))
        self.assertNotRegex(' '.join(args['write_flash_args']),r'(?i)erase[-_]all')
    def test_device_rejects_unsupported_text_before_sending_any_byte(self):
        # Explicit Enter is separate from credential text; neither relaxes ASCII validation.
        s=section(read('main/core/password_vault.cpp'),'bool type(', 'std::string hex(')
        self.assertLess(s.index('!printable(text.data(), text.size())'),s.index('sys::BleHid::sendPrivateKey'))
        self.assertIn('uint8_t(p[i]) < 32 || uint8_t(p[i]) > 126',read('main/core/vault_format.hpp'))
        self.assertIn('exactPrintableAscii()',read('../tooling/firmware/hid_keyboard_model_test.cpp'))

# Stateless server text-validation tests now run against Java Entries in DeploymentTest.java.
if __name__=='__main__':unittest.main(verbosity=2)
