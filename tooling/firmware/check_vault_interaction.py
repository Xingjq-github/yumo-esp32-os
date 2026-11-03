"""Device vault interaction contracts. Synthetic/offline; no serial or server access."""
from pathlib import Path
import hashlib
import json
import re
import unittest
ROOT = (Path(__file__).resolve().parents[2]/'firmware')
BASELINE = ROOT / '../tooling/firmware/fixtures/contracts/vault-interaction-20260918/before.json'
def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')
def compact(text):
    return re.sub(r'\s+', '', text)
def section(text, start, end):
    return text[text.index(start):text.index(end, text.index(start) + len(start))]
class VaultInteractionContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = compact(read('main/apps/password_vault.cpp'))
        cls.core = compact(read('main/core/password_vault.cpp'))
    def test_account_sends_directly_only_from_detail(self):
        s = section(self.app, 'caseAction::User:', 'caseAction::Password:')
        self.assertIn('m_page==Page::Detail&&m_session&&m_selected<m_session->entries.size()', s)
        self.assertIn('m_session->entries[m_selected].category!="other")startTyping(true,sys::BleHid::privateLinkGeneration())', s)
        self.assertNotIn('ConfirmType', s)
    def test_password_sends_directly_only_from_valid_detail(self):
        s = section(self.app, 'caseAction::Password:', 'caseAction::Enter:')
        guard = 'if(m_page!=Page::Detail||!m_session||m_selected>=m_session->entries.size())return;'
        send = 'startTyping(false,sys::BleHid::privateLinkGeneration());'
        self.assertIn(guard, s)
        self.assertIn(send, s)
        self.assertLess(s.index(guard), s.index(send))
        self.assertEqual(s.count('startTyping('), 1)
        for old in ('ConfirmType', 'm_targetLink', 'Action::Type', '确认并立即输入'):
            self.assertNotIn(old, self.app)
            self.assertNotIn(old, read('main/apps/password_vault.hpp'))
    def test_typing_rechecks_target_and_cleans_failed_launch(self):
        s = section(self.app, 'voidPasswordVault::startTyping(', 'voidPasswordVault::drawPin(')
        self.assertIn('m_reveal.hide()', s)
        self.assertIn('!m_session||m_selected>=m_session->entries.size()', s)
        self.assertIn('!link||link!=sys::BleHid::privateLinkGeneration()', s)
        self.assertIn('job->link=link', s)
        self.assertIn('job->permit=sys::BleHid::beginPrivateInput()', s)
        self.assertIn('if(launch(job)!=vault::StartResult::Started)sys::BleHid::cancelPrivateInput()', s)
        self.assertIn('catch(conststd::bad_alloc&){sys::BleHid::cancelPrivateInput()', s)
    def test_character_loop_keeps_release_and_cancellation_guards(self):
        s = section(self.core, 'booltype(', 'std::stringhex(')
        for token in ('text.empty()||text.size()>128', 'printable(text.data(),text.size())',
                      'stopped(job)||sys::BleHid::privateLinkGeneration()!=job.link',
                      'sendPrivateKey(key.code,key.modifiers,job.link,job.permit)!=ESP_OK',
                      'job.progress.store(i+1)'):
            self.assertIn(token, s)
        self.assertLess(s.index('sendPrivateKey('), s.index('job.progress.store'))
        self.assertLess(s.index('job.progress.store'), s.index('vTaskDelay'))
    def test_no_preparation_wait_and_no_final_character_delay(self):
        s = section(self.core, 'booltype(', 'std::stringhex(')
        self.assertEqual(s.count('vTaskDelay('), 1)
        self.assertIn('if(i+1<text.size())vTaskDelay(pdMS_TO_TICKS(interaction::KeyGapMs))', s)
        for old in ('pdMS_TO_TICKS(100)', 'pdMS_TO_TICKS(35)', 'i<30'):
            self.assertNotIn(old, s)
        self.assertIn('KeyGapMs=10', compact(read('main/core/vault_interaction.hpp')))
    def test_reveal_click_opens_pin_without_grant(self):
        s = section(self.app, 'caseAction::Reveal:', 'caseAction::VerifyReveal:')
        self.assertIn('m_page!=Page::Detail', s)
        self.assertIn('if(m_reveal.visible(m_selected)){m_reveal.hide();}', s)
        self.assertIn('vault::clear(m_pin);vault::clear(m_firstPin);m_page=Page::VerifyReveal', s)
        self.assertNotIn('m_reveal.complete', s)
        self.assertNotIn('m_reveal.request', s)
    def test_reveal_submit_requires_valid_pin_and_owned_entry(self):
        s = section(self.app, 'caseAction::VerifyReveal:', 'caseAction::Sync:')
        for token in ('m_page!=Page::VerifyReveal', '!m_session',
                      'm_selected>=m_session->entries.size()',
                      '!vault::pattern::valid(m_pin.data(),m_pin.size())', 'vault::Operation::VerifyPin',
                      'job->input=m_session;job->entry=m_selected;job->master=m_pin;',
                      'vault::clear(m_pin);m_reveal.request(m_selected)',
                      'if(launch(job)!=vault::StartResult::Started)rejectReveal(nullptr)',
                      'catch(conststd::bad_alloc&){rejectReveal("内存不足，请稍后重试")'):
            self.assertIn(token, s)
    def test_worker_pin_check_precedes_any_disk_load(self):
        s = section(self.core, 'voidrun(', 'voidworker(')
        gate = 'if(job.operation==Operation::VerifyPin){'
        self.assertLess(s.index(gate), s.index('Diskdisk;'))
        verify = section(s, gate, 'Diskdisk;')
        self.assertIn('!job.input||job.entry>=job.input->entries.size()', verify)
        self.assertIn('job.ok=verifyPin(job,*job.input)&&!stopped(job);return;', verify)
        for forbidden in ('load(', 'decode(', 'commit(', 'erase(', 'encrypt', 'job.output'):
            self.assertNotIn(forbidden, verify)
    def test_unlock_and_reveal_share_existing_pin_hash_and_cooldown(self):
        s = section(self.core, 'boolverifyPin(', 'voidcopyConfig(')
        for token in ('session.pinSalt', 'session.pinHash', 'Keyexpected,verifier;',
                      'derive(job.master.c_str(),job.master.size(),salt,verifier,job.cancelled)',
                      'equal(verifier.bytes.data(),expected.bytes.data(),32)',
                      'failures=std::min(failures+1,5u)', 'nextUnlock=esp_timer_get_time()',
                      'failures=0;nextUnlock=0;'):
            self.assertIn(token, s)
        self.assertIn('if(!verifyPin(job,*result))return;', self.core)
        s = section(self.core, 'voidrun(', 'Diskdisk;')
        self.assertIn('job.operation==Operation::Unlock||job.operation==Operation::WebUnlock||job.operation==Operation::VerifyPin', s)
        self.assertIn('esp_timer_get_time()<nextUnlock', s)
        self.assertIn('pattern::valid(job.master.data(),job.master.size())', s)
    def test_only_successful_uncancelled_result_can_show_password(self):
        s = section(self.app, 'voidPasswordVault::poll()', 'voidPasswordVault::draw()')
        self.assertIn('if(m_job&&m_job->done.load())', s)
        self.assertIn('boolsuccess=m_job->ok&&!m_job->cancelled.load();if(success){switch(kind)', s)
        verify = section(s, 'casevault::Operation::VerifyPin:', 'casevault::Operation::Type:')
        self.assertIn('m_reveal.complete(m_page==Page::VerifyReveal&&m_session&&m_selected==m_job->entry,m_job->entry,lv_tick_get())', verify)
        self.assertIn('m_page=Page::Detail', verify)
        self.assertIn('lv_display_trigger_activity(nullptr)', verify)
        self.assertIn('if(kind==vault::Operation::VerifyPin)rejectReveal(m_job->error.empty()?"操作已取消":m_job->error.c_str())', s)
        self.assertIn('m_reveal.hide()', section(self.app, 'voidPasswordVault::rejectReveal(', 'lv_obj_t*PasswordVault::button('))
        self.assertEqual(self.app.count('m_reveal.complete('), 1)
    def test_secret_label_always_uses_entry_scoped_visibility(self):
        s = section(self.app, 'casePage::Detail:', 'casePage::Qr:')
        self.assertIn('(m_access.super()||m_reveal.visible(m_selected))?entry.password.c_str():"********"', s)
        self.assertIn('m_reveal.visible(m_selected)?"隐藏密码":"展示密码"', s)
    def test_other_codes_are_visible_and_have_no_account_or_reveal_controls(self):
        detail = section(self.app, 'casePage::Detail:', 'casePage::Qr:')
        code = section(detail, 'if(entry.category=="other"){', '}else{')
        for token in ('button(actions,entry.password.c_str(),Action::Password',
                      '"回车",Action::Enter', '"二维码",Action::Qr'):
            self.assertIn(token, code)
        for forbidden in ('entry.username', 'Action::User', 'Action::Reveal', 'm_reveal', 'm_access', '"********"'):
            self.assertNotIn(forbidden, code)
        update = section(self.app, 'voidPasswordVault::updateListRows()', 'voidPasswordVault::styleCategory(')
        self.assertIn('entry.category=="other"?entry.password.c_str():entry.username.c_str()', update)
        reveal = section(self.app, 'caseAction::Reveal:', 'caseAction::VerifyReveal:')
        self.assertIn('m_session->entries[m_selected].category=="other")return;', reveal)
    def test_cancel_back_lock_and_entry_switch_revoke_reveal(self):
        self.assertIn('vault::cancel(m_job);m_reveal.hide()', section(self.app,'if(a==Action::Cancel)', 'if(a==Action::Back)'))
        for start, end in (('voidPasswordVault::lock()', 'voidPasswordVault::onHide()'),
                           ('boolPasswordVault::onBack()', 'vault::StartResultPasswordVault::launch('),
                           ('caseAction::Detail:', 'caseAction::User:')):
            self.assertIn('m_reveal.hide()', section(self.app, start, end))
        back = section(self.app, 'boolPasswordVault::onBack()', 'vault::StartResultPasswordVault::launch(')
        self.assertIn('vault::clear(m_pin);vault::clear(m_firstPin)', back)
        self.assertIn('m_page==Page::VerifyReveal', back)
    def test_screen_lock_idle_timeout_and_worker_cleanup_retained(self):
        for token in ('sys::EventType::ScreenOff', 'lv_display_get_inactive_time(nullptr)>=60000',
                      'vault::cancel(m_job);m_job.reset();m_session.reset();resetListIndex();clearListWidgets();wipeField()',
                      'if(m_reveal.expire(lv_tick_get()))m_redraw=true'):
            self.assertIn(token, self.app)
        for token in ('clear(job->master);job->input.reset()',
                      'if(stopped(*job))',
                      'job->ok=false;job->output.reset()', 'sys::Power::WorkGuardguard;'):
            self.assertIn(token, self.core)
    def test_one_compact_pin_renderer_for_setup_unlock_and_reveal(self):
        self.assertEqual(self.app.count('drawPin('), 6)  # includes disabled loading/verification view
        s = section(self.app, 'voidPasswordVault::drawPin(', 'voidPasswordVault::timer(')
        self.assertIn('m_pattern.create(m_body,patternPrompt(),ready,patternSubmitted,this)',s)
        self.assertIn('!m_job&&(m_page!=Page::Locked||(m_load.ready()&&(m_exists||m_blank)))',s)
        self.assertIn('m_body=c::panel(root(),L::Width,pinPage?L::BodyHeight:L::NormalBodyHeight,!pinPage)', self.app)
        self.assertNotIn('Action::Digit',self.app)
        self.assertNotIn('Action::DeleteDigit',self.app)
    def test_layout_respects_actual_font_metrics(self):
        cjk = read('main/ui/lv_font_cjk_16.c')
        mask = read('../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_14.c')
        self.assertRegex(cjk, r'\.line_height\s*=\s*21,')
        self.assertRegex(mask, r'\.line_height\s*=\s*16,')
        self.assertIn('#include "core/vault_interaction.hpp"', read('../tooling/firmware/vault_interaction_test.cpp'))
    def test_ble_transport_and_nonvault_firmware_unchanged(self):
        before = json.loads(BASELINE.read_text(encoding='utf-8'))['files']
        permitted = {'main/apps/password_vault.cpp', 'main/apps/password_vault.hpp',
                     'main/core/password_vault.cpp', 'main/core/password_vault.hpp',
                     '../tooling/firmware/check_password_vault.py', '../tooling/firmware/vault_simple_test.cpp',
                     # Subsequent authorized Wi-Fi/system-input layout release.
                     'main/ui/text_input.cpp', 'main/ui/compact_ui.hpp',
                     'main/apps/settings_wifi.cpp', 'main/main.cpp',
                     'main/apps/settings_system.cpp', '../tooling/firmware/check_ui_power.py',
                     '../tooling/firmware/check_firmware_refactor.py',
                     # Authorized single-row PIN/BLE persistence follow-up.
                     'main/core/ble_hid.cpp', 'sdkconfig', 'sdkconfig.defaults',
                     '../tooling/firmware/check_manual_lifecycle.py', '../tooling/firmware/hid_keyboard_model_test.cpp',
                     # Authorized fullscreen PIN/reset removal; new checks freeze storage I/O.
                     'main/core/vault_storage.cpp', 'main/core/vault_storage.hpp',
                     '../tooling/firmware/check_vault_loading.py'}
        for name, digest in before.items():
            if name in permitted or name.startswith('../artifacts/firmware/'):
                continue
            self.assertTrue((ROOT / name).is_file(), name)
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest, name)
if __name__ == '__main__':
    unittest.main(verbosity=2)
