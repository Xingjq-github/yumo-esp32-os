"""Reveal-PIN failure navigation contracts; source-only, no compiler or device I/O."""
from pathlib import Path
import hashlib
import json
import unittest

from check_vault_interaction import compact, section

ROOT = (Path(__file__).resolve().parents[2]/'firmware')
RUN = ROOT / '../tooling/firmware/fixtures/contracts/vault-reveal-return-20260918'


class VaultRevealReturnContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = (ROOT / 'main/apps/password_vault.cpp').read_text(encoding='utf-8-sig')
        cls.app = compact(cls.raw)
        cls.reject = section(cls.app, 'voidPasswordVault::rejectReveal(',
                             'voidPasswordVault::finishAccess(')
        cls.submit = section(cls.app, 'caseAction::VerifyReveal:', 'caseAction::Sync:')
        cls.poll = section(cls.app, 'voidPasswordVault::poll()', 'voidPasswordVault::draw()')
        cls.baseline = json.loads((RUN / 'before.json').read_text(encoding='utf-8'))

    def test_failure_revokes_grant_clears_pin_and_returns_to_detail(self):
        for token in ('m_reveal.hide();', 'm_redraw=true;',
                      'vault::clear(m_pin);vault::clear(m_firstPin);',
                      'm_page=Page::Detail;', 'if(text)message(text);'):
            self.assertIn(token, self.reject)
        self.assertLess(self.reject.index('m_reveal.hide()'), self.reject.index('m_page=Page::Detail'))
        self.assertLess(self.reject.index('vault::clear(m_pin)'), self.reject.index('m_page=Page::Detail'))
        self.assertLess(self.reject.index('m_redraw=true'), self.reject.index('return;'))
        for forbidden in ('m_reveal.complete', 'm_reveal.request', 'm_selected=',
                          'm_session=', 'm_session.reset', 'vault::start', 'launch('):
            self.assertNotIn(forbidden, self.reject)

    def test_failure_never_reopens_an_unrelated_or_locked_page(self):
        guard = 'if(m_page!=Page::VerifyReveal)return;'
        self.assertIn(guard, self.reject)
        self.assertLess(self.reject.index(guard), self.reject.index('vault::clear(m_pin)'))
        self.assertLess(self.reject.index(guard), self.reject.index('m_page=Page::Detail'))
        self.assertLess(self.reject.index(guard), self.reject.index('message(text)'))

    def test_missing_session_or_deleted_entry_locks_instead_of_showing_detail(self):
        guard = 'if(!m_session||m_selected>=m_session->entries.size()){lock();return;}'
        self.assertIn(guard, self.reject)
        self.assertLess(self.reject.index(guard), self.reject.index('m_page=Page::Detail'))

    def test_empty_short_or_invalid_pin_returns_without_launching_a_job(self):
        invalid = section(self.submit, 'if(!vault::validPin(', 'try{')
        self.assertIn('rejectReveal("验证失败，请输入4到12位数字");return;', invalid)
        self.assertNotIn('launch(', invalid)
        self.assertNotIn('m_reveal.request(', invalid)
        self.assertLess(self.submit.index('!vault::validPin'), self.submit.index('std::make_shared'))

    def test_launch_and_allocation_failures_also_leave_verification(self):
        self.assertIn('if(launch(job)!=vault::StartResult::Started)rejectReveal(nullptr);', self.submit)
        self.assertIn('catch(conststd::bad_alloc&){rejectReveal("内存不足，请稍后重试");}', self.submit)
        # nullptr deliberately keeps launch()'s busy/memory/sleep-specific message.
        self.assertIn('if(text)message(text);', self.reject)
        self.assertNotIn('m_message.clear()', self.reject)

    def test_failed_pin_cooldown_and_cancelled_jobs_use_same_rejection(self):
        self.assertIn('boolsuccess=m_job->ok&&!m_job->cancelled.load();if(success)', self.poll)
        self.assertIn('if(kind==vault::Operation::VerifyPin)rejectReveal(m_job->error.empty()?"操作已取消":m_job->error.c_str());elsemessage(m_job->error.empty()?"操作已取消":m_job->error.c_str());', self.poll)
        self.assertIn('m_job.reset();m_redraw=true;', self.poll)

    def test_valid_result_requires_current_entry_and_rejected_result_returns(self):
        success = section(self.poll, 'casevault::Operation::VerifyPin:', 'casevault::Operation::Type:')
        self.assertIn('m_reveal.complete(m_page==Page::VerifyReveal&&m_session&&m_selected==m_job->entry,m_job->entry,lv_tick_get())', success)
        self.assertIn('m_page=Page::Detail;m_message.clear();lv_display_trigger_activity(nullptr);', success)
        self.assertIn('else{rejectReveal("查看已取消");}', success)
        self.assertEqual(self.app.count('m_reveal.complete('), 1)

    def test_returned_detail_has_message_mask_and_original_entry(self):
        draw = section(self.app, 'voidPasswordVault::draw()', 'casePage::Locked:')
        self.assertIn('if(!m_message.empty()&&!pinPage&&!purposePage())wrap(m_body,m_message.c_str());', draw)
        detail = section(self.app, 'casePage::Detail:', 'casePage::ConfirmType:')
        self.assertIn('constauto&entry=m_session->entries[m_selected];', detail)
        self.assertIn('(m_access.super()||m_reveal.visible(m_selected))?entry.password.c_str():"********"', detail)
        self.assertIn('m_reveal.visible(m_selected)?"隐藏密码":"展示密码"', detail)
        self.assertNotIn('m_selected=', self.reject)

    def test_unlock_bluetooth_layout_typing_and_lifecycle_sections_unchanged(self):
        reviewed = json.loads((ROOT/'../tooling/firmware/fixtures/contracts/vault-device-super-20260922/sections.json').read_text())
        for item in self.baseline['preserved_app_sections']:
            with self.subTest(section=item['start']):
                if item['start'] in ('void PasswordVault::lock()', 'void PasswordVault::draw()'):
                    current=json.loads((ROOT/'../tooling/firmware/fixtures/contracts/vault-lan-web-20261020/sections.json').read_text())
                    actual=section(self.raw,item['start'],item['end'])
                    self.assertEqual(hashlib.sha256(actual.encode()).hexdigest(),current[item['start']])
                    continue
                if item['start'] in reviewed:
                    actual = section(self.raw, item['start'], item['end'])
                    if item['start']=='void PasswordVault::lock()':
                        # This section also contains onBack(). Only the new
                        # work/app subgroup -> group navigation is normalized;
                        # lock, PIN cleanup and every other lifecycle byte remain
                        # frozen. Target membership/reset has check_vault_list.
                        target_back = '''  if (m_page == Page::List && targetCategory() && !targetGroups()) {
    m_access.leavePage();
    m_targetGroup = vault::interaction::TargetIndex::None;
    selectList();
    m_message.clear();
    m_redraw = true;
    return true;
  }
'''
                        self.assertEqual(actual.count(target_back),1)
                        actual=actual.replace(target_back,'')
                    if item['start']=='void PasswordVault::draw()':
                        # Other is now a visible code, independently checked by
                        # check_vault_interaction. Normalize only that branch;
                        # retain the byte-level freeze for all password paths.
                        start=actual.index('      if (entry.category == "other") {')
                        end=actual.index('        break;\n      }\n',start)+len('        break;\n      }\n')
                        actual=actual[:start]+actual[end:]
                        prompt='''    wrap(m_body, m_session && m_selected < m_session->entries.size() &&
                    m_session->entries[m_selected].category == "other"
                    ? "将向此设备输入码值" : "将向此设备输入密码");'''
                        self.assertEqual(actual.count(prompt),1)
                        actual=actual.replace(prompt,'    wrap(m_body, "将向此设备输入密码");')
                        # The sole reviewed theme hook is presentation-only;
                        # all other bytes of the security-sensitive draw path stay frozen.
                        actual=actual.replace('      styleCategory(row, name, detail, k);\n','')
                        # Tiles distributes the former bottom gap to existing buttons.
                        # Normalize only heights; callbacks and access checks stay frozen.
                        height='    const int toolbarHeight = ui::theme::tiles() ? 44 : 36;\n'
                        self.assertEqual(actual.count(height),1)
                        actual=actual.replace(height,'').replace('toolbarHeight','36')
                        # Background + foreground now change together. Normalize
                        # only these two reviewed visual calls, never access logic.
                        for button in ('eye','super'):
                            new=f'c::buttonBackground({button}, ui::theme::accent());'
                            self.assertEqual(actual.count(new),1)
                            actual=actual.replace(new,f'lv_obj_set_style_bg_color({button}, ui::theme::accent(), 0);')
                        # Reviewed prompt-only changes; all access logic stays frozen.
                        for new,old in (
                            ('密码区校验失败，原数据已保留。请重新读取。', '密码区数据无效，请重新读取或检查固件，原数据已保留。'),
                            ('暂无密码，请在服务端添加后同步', '此分类暂无密码，请在服务端添加后同步'),
                            ('与服务端连接同一局域网，填写服务地址。', '电脑和设备须在同一局域网。迁移NAS后修改此地址。'),
                            ('选中目标输入框，切到US英文布局，关闭大写锁定。', '请先选中目标输入框，使用US英文布局，关闭大写锁定。'),
                            ('开机尝试最近连接；手动断开蓝牙后不再自动连接。', '网络和蓝牙需手动连接，断开后不会自动重连。'),
                        ):
                            self.assertEqual(actual.count(new),1)
                            actual=actual.replace(new,old)
                    self.assertEqual(hashlib.sha256(actual.encode()).hexdigest(), reviewed[item['start']])
                    continue
                from check_vault_change_password import prior_change_ui
                from check_vault_list import prior_list_ui
                text = prior_change_ui(prior_list_ui(section(self.raw, item['start'], item['end'])))
                if item['start']=='void PasswordVault::requestBluetooth(':
                    # Only eligibility changed: a manual request now owns
                    # teardown/switching. Preserve the other sensitive bytes.
                    text=text.replace('const bool discovering = state.discovering();','const bool discovering = state.discoverableSeconds != 0;')
                    text=text.replace('      state.state != sys::BleHidState::Starting;','      state.state != sys::BleHidState::Starting &&\n      state.state != sys::BleHidState::Connecting &&\n      state.state != sys::BleHidState::Connected;')
                    text=text.replace('请先打开蓝牙，等待就绪','请先打开蓝牙，等待就绪并断开当前连接')
                # Undo reviewed Wi-Fi/FC lifecycle and network-field layout edits; compare
                # every other byte of these sensitive sections to the old hash.
                # New cancellation/error semantics have dedicated contracts.
                text = text.replace('    sys::SystemService::cancelBluetoothConnection();',
                                    '    (void)sys::SystemService::request(sys::ServiceAction::BluetoothDiscover,\n                                      false);')
                text = text.replace('message(sys::SystemService::errorText(result) ? sys::SystemService::errorText(result) :\n            "蓝牙请求未接受，请稍后重试");',
                                    'message("蓝牙请求未接受，请稍后重试");')
                if item['start'] == 'void PasswordVault::draw()':
                    new_field = ('    m_field = c::networkField(m_body, "http://地址:端口",\n'
                                 '                              m_session ? m_session->url.c_str() : "", 240,\n'
                                 '                              &lv_font_montserrat_14);')
                    old_field = ('    m_field = c::field(m_body, "http://地址:端口",\n'
                                 '                       m_session ? m_session->url.c_str() : "", 240);\n'
                                 '    lv_obj_set_style_text_font(m_field, &lv_font_montserrat_14, 0);')
                    self.assertEqual(text.count(new_field), 1)
                    text = text.replace(new_field, old_field)
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), item['sha256'])
        self.assertNotIn('rejectReveal(', section(self.app, 'caseAction::Unlock:', 'caseAction::Category:'))

    def test_only_vault_ui_source_changed_and_build_remains_untouched(self):
        changed = set()
        for name in self.baseline['firmware_inputs']:
            from vault_release_contract import source_before_release
            actual = hashlib.sha256(source_before_release(name)).hexdigest()
            if actual != self.baseline['files'][name]['sha256']:
                changed.add(name)
        self.assertEqual(changed, {'main/apps/password_vault.cpp', 'main/apps/password_vault.hpp',
                                   'main/core/password_vault.cpp', 'main/core/password_vault.hpp'})
        self.assertEqual(len(self.baseline['build_files']), 7)
        from vault_release_contract import expected_build_hash
        for name in self.baseline['build_files']:
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(),
                             expected_build_hash(name,self.baseline['files'][name]['sha256']), name)


if __name__ == '__main__':
    unittest.main(verbosity=2)
