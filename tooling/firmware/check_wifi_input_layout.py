"""System input/Wi-Fi layout contracts; no network, serial or user data access."""
from pathlib import Path
import hashlib
import json
import re
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BASELINE=ROOT/'../tooling/firmware/fixtures/contracts/wifi-input-build-flash-20260918-1236/before.json'
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(s):return re.sub(r'\s+','',s)
class WifiInputLayoutContracts(unittest.TestCase):
    def test_two_rows_have_requested_order_and_balanced_widths(self):
        s=compact(read('main/ui/text_input.cpp'))
        self.assertIn('lv_obj_set_flex_flow(bar,LV_FLEX_FLOW_COLUMN)',s)
        self.assertIn('type=c::dropdown(selectors,',s)
        self.assertIn('character=c::dropdown(selectors,',s)
        self.assertIn('c::button(actions,"删除",edit,reinterpret_cast<void*>(1),L::LeftWidth,L::RowHeight)',s)
        self.assertIn('c::button(actions,"输入",edit,nullptr,L::RightWidth,L::RowHeight)',s)
        self.assertLess(s.index('type=c::dropdown'),s.index('character=c::dropdown'))
        self.assertLess(s.index('c::button(actions,"删除"'),s.index('c::button(actions,"输入"'))
        self.assertNotIn('退出',s)
    def test_enlarged_geometry_is_used_by_production_and_model(self):
        s=compact(read('main/ui/text_input.cpp'))
        self.assertIn('bar=c::panel(lv_layer_top(),L::Width,L::Height)',s)
        self.assertIn('lv_obj_set_height(control,L::RowHeight)',s)
        self.assertIn('kInput=TextInputLayout::Height',read('main/ui/compact_ui.hpp'))
        self.assertIn('#include "ui/text_input_layout.hpp"',read('../tooling/firmware/text_input_layout_test.cpp'))
    def test_wifi_height_restored_after_one_line_field_creation(self):
        shared=compact(read('main/ui/compact_ui.cpp'))
        s=shared.split('lv_obj_t*networkField(',1)[1].split('lv_obj_t*dropdown(',1)[0]
        self.assertLess(s.index('obj=field('),s.index('lv_obj_set_height(obj,height)'))
        self.assertIn('height=TextInputLayout::NetworkFieldHeight',s)
        self.assertIn('lv_obj_set_style_pad_top(obj,pad>0?pad:0,0)',s)
        self.assertIn('lv_obj_set_style_pad_bottom(obj,pad>0?pad:0,0)',s)
        self.assertLess(s.index('if(font)lv_obj_set_style_text_font'),s.index('font=lv_obj_get_style_text_font'))
        s=compact(read('main/apps/settings_wifi.cpp'))
        self.assertIn('v.ssid=c::networkField',s)
        self.assertIn('v.password=c::networkField',s)
        self.assertIn('lv_textarea_set_password_mode(v.password,false)',s)
        vault=compact(read('main/apps/password_vault.cpp')).split('casePage::Address:',1)[1].split('casePage::Detail:',1)[0]
        self.assertRegex(vault,r'm_field=c::networkField\([^;]+,&lv_font_montserrat_14\)')
    def test_dropdown_text_and_popup_fit_enlarged_bar(self):
        s=compact(read('main/ui/text_input.cpp'))
        self.assertIn('L::RowHeight-int(font->line_height)',s)
        self.assertIn('lv_obj_set_style_pad_top(control,pad,0)',s)
        self.assertIn('lv_obj_set_style_max_height(lv_dropdown_get_list(control),L::Top-L::Gap,0)',s)
        self.assertIn('lv_dropdown_set_dir(obj,LV_DIR_TOP)',read('main/ui/compact_ui.cpp'))
    def test_viewport_reserves_whole_two_row_height_and_restores(self):
        s=compact(read('main/ui/text_input.cpp'))
        self.assertIn('TextInputLayout::viewport(lv_obj_get_height(viewport),area.y1)',s)
        self.assertIn('viewportHeight=lv_obj_get_style_height(viewport,LV_PART_MAIN)',s)
        self.assertIn('lv_obj_set_height(viewport,viewportHeight)',s)
        self.assertIn('lv_obj_scroll_to_view(field,LV_ANIM_OFF)',s)
    def test_external_keyboard_and_delete_lifecycle_remain(self):
        s=compact(read('main/ui/text_input.cpp'))
        for token in ('if(sys::BleHid::keyboardConnected()&&!sys::BleKeyboardClient::iineKeyboard()){hide();return;}',
                      'if(obj==viewport)viewport=nullptr;',
                      'if(obj==target){target=nullptr;TextInput::hide();}',
                      'lv_obj_remove_event_cb(target,deleted)',
                      'lv_obj_remove_event_cb(viewport,deleted)',
                      'lv_dropdown_close(type)','lv_dropdown_close(character)'):
            self.assertIn(token,s)
    def test_text_and_dropdown_clicks_do_not_hide_input_bar(self):
        s=compact(read('main/ui/text_input.cpp'))
        self.assertIn('within(obj,bar)||within(obj,lv_dropdown_get_list(type))||within(obj,lv_dropdown_get_list(character))',s)
        self.assertIn('lv_obj_check_type(field,&lv_textarea_class)',s)
        self.assertIn('TextInput::show(field);return;',s)
        self.assertIn('lv_indev_add_event_cb(pointer,pointerClick,LV_EVENT_CLICKED',s)
    def test_delete_and_insert_preserve_character_and_space_behavior(self):
        s=compact(read('main/ui/text_input.cpp'))
        self.assertIn('if(lv_event_get_user_data(e))lv_textarea_delete_char(target)',s)
        self.assertIn('std::strcmp(text,"空格")==0?" ":text',read('main/ui/text_input.cpp'))
        for charset in ('0123456789','ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz'):
            self.assertIn(charset,s)
        self.assertIn('!target||!shown||!sys::Power::isScreenOn()',s)
    def test_unrelated_firmware_remains_byte_identical_after_pin_ble_followup(self):
        data=json.loads(BASELINE.read_text(encoding='utf-8'))['files']
        allowed={'main/ui/text_input.cpp','main/ui/compact_ui.hpp','main/apps/settings_wifi.cpp',
                 'main/main.cpp','main/apps/settings_system.cpp','../tooling/firmware/check_ui_power.py',
                 '../tooling/firmware/check_firmware_refactor.py','../tooling/firmware/check_vault_interaction.py',
                 # Follow-up has dedicated scope/invariant coverage.
                 'main/apps/password_vault.cpp','main/apps/password_vault.hpp',
                 'main/core/vault_interaction.hpp','main/core/ble_hid.cpp',
                 'sdkconfig','sdkconfig.defaults','../tooling/firmware/check_manual_lifecycle.py',
                 '../tooling/firmware/vault_interaction_test.cpp','../tooling/firmware/hid_keyboard_model_test.cpp',
                 # Authorized explicit Enter/reset removal, checked by new scoped contracts.
                 'main/core/password_vault.cpp','main/core/password_vault.hpp',
                 'main/core/vault_storage.cpp','main/core/vault_storage.hpp',
                 '../tooling/firmware/check_vault_loading.py'}
        for name,expected in data.items():
            if name in allowed or name.startswith('../artifacts/firmware/'):continue
            self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),expected['sha256'],name)
    def test_revision_pair_matches_authorized_vault_enter_release(self):
        data=json.loads(BASELINE.read_text(encoding='utf-8'))['files']
        for name in ('main/main.cpp','main/apps/settings_system.cpp'):
            from vault_release_contract import source_before_release
            text=source_before_release(name)
            self.assertEqual(text.count(b'VAULT-ENTER-20260918'),1)
            if name=='main/apps/settings_system.cpp':
                self.assertEqual(hashlib.sha256(text.replace(b'VAULT-ENTER-20260918',b'VAULT-LOAD-20260918')).hexdigest(),data[name]['sha256'])
            # main.cpp's only additional delta is non-destructive NVS startup,
            # checked byte-for-byte in check_vault_pin_ble.py.
if __name__=='__main__':unittest.main(verbosity=2)
