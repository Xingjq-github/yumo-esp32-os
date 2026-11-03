"""Device super/page access and direct web input integration contracts."""
from pathlib import Path
import unittest
from check_vault_interaction import compact, section
ROOT = (Path(__file__).resolve().parents[2]/'firmware')

class VaultAccessContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = compact((ROOT/'main/apps/password_vault.cpp').read_text(encoding='utf-8'))
        cls.core = compact((ROOT/'main/core/password_vault.cpp').read_text(encoding='utf-8'))

    def test_main_toolbar_and_child_header(self):
        main = section(self.app, 'casePage::Categories:', 'casePage::List:')
        for text in ('"同步",Action::Sync,0,79', '"连接",Action::Network,0,79',
                     '"超管",Action::Super,0,80', 'if(m_access.super())c::buttonBackground'):
            self.assertIn(text, main)
        head = section(self.app, 'voidPasswordVault::draw()', 'if(m_job)')
        self.assertIn('if(m_session&&m_page==Page::Categories)', head)
        self.assertEqual(head.count('Action::ChangePassword'), 1)
        self.assertIn('elseif(purposePage())', head)
        self.assertLess(head.index('Action::PageReveal'), head.index('Action::Enter'))
        self.assertIn('c::disabled(eye,m_access.super())', head)
        self.assertIn('lv_obj_set_height(title,L::HeaderHeight)', head)

    def test_access_checks_existing_pin_off_ui_and_no_storage_write(self):
        gate = section(self.core, 'if(job.operation==Operation::Setup||', 'Diskdisk;')
        self.assertIn('job.operation==Operation::VerifyAccess', gate)
        self.assertIn('esp_timer_get_time()<nextUnlock', gate)
        check = section(gate, 'if(job.operation==Operation::VerifyChangePin||', 'if(job.operation==Operation::ChangePin&&')
        self.assertIn('if(!job.input)', check)
        self.assertIn('job.ok=verifyPin(job,*job.input)&&!stopped(job);return;', check)
        for forbidden in ('entries', 'commit(', 'load(', 'job.output'):
            self.assertNotIn(forbidden, check)
        action = section(self.app, 'caseAction::VerifyAccess:', 'caseAction::ChangePassword:')
        self.assertLess(action.index('!vault::pattern::valid'), action.index('std::make_shared'))
        self.assertIn('job->entry=m_accessScope', action)
        self.assertIn('vault::clear(m_pin)', action)
        self.assertIn('catch(conststd::bad_alloc&)', action)

    def test_grant_requires_owned_job_current_page_scope_and_success(self):
        poll = section(self.app, 'voidPasswordVault::poll()', 'voidPasswordVault::resetListIndex()')
        self.assertIn('boolsuccess=m_job->ok&&!m_job->cancelled.load();if(success)', poll)
        success = section(poll, 'casevault::Operation::VerifyAccess:', 'casevault::Operation::VerifyChangePin:')
        self.assertIn('finishAccess(m_job->entry==m_accessScope', success)
        # The worker releases input before publishing done; don't compare it here.
        self.assertNotIn('m_job->input', success)
        finish = section(self.app, 'voidPasswordVault::finishAccess(', 'lv_obj_t*PasswordVault::button(')
        self.assertIn('m_page!=Page::VerifyAccess||!m_session', finish)
        self.assertIn('m_accessScope==m_webAccount', finish)
        self.assertIn('m_access.complete(verified&&current,m_accessScope)', finish)
        self.assertIn('m_page=global?Page::Categories:Page::List', finish)
        self.assertIn('finishAccess(false,m_job->error.empty()', poll)

    def test_lock_exit_and_account_navigation_revoke_correct_grants(self):
        lock = section(self.app, 'voidPasswordVault::lock()', 'voidPasswordVault::onHide()')
        self.assertIn('vault::cancel(m_job);m_job.reset();m_session.reset()', lock)
        self.assertIn('m_access.lock()', lock)
        self.assertIn('m_accessScope=vault::interaction::AccessState::None', lock)
        back = section(self.app, 'boolPasswordVault::onBack()', 'vault::StartResultPasswordVault::launch(')
        self.assertIn('finishAccess(false,"验证已取消")', back)
        self.assertIn('!webAccounts()){m_access.leavePage()', back)
        select = section(self.app, 'caseAction::Account:', 'caseAction::PurposeUser:')
        self.assertIn('m_access.leavePage()', select)
        typing = section(self.app, 'voidPasswordVault::startTyping(', 'voidPasswordVault::requestBluetooth(')
        self.assertNotIn('m_access.leavePage()', typing)
        self.assertIn('m_typePage=purposePage()?Page::List:Page::Detail', typing)

    def test_direct_input_uses_real_group_entry_and_stable_private_transport(self):
        direct = section(self.app, 'caseAction::PurposeUser:', 'caseAction::Detail:')
        for token in ('!purposePage()||!m_session', 'index=m_webIndex.entry(m_webAccount)',
                      'index>=m_session->entries.size()', 'm_webIndex.groupFor(index)!=m_webAccount',
                      'm_selected=index', 'startTyping(a==Action::PurposeUser,sys::BleHid::privateLinkGeneration())'):
            self.assertIn(token, direct)
        self.assertNotIn('ConfirmType', direct)
        click = section(self.app, 'voidPasswordVault::clicked(', 'voidPasswordVault::action(')
        self.assertIn('Action::PurposePassword', click)
        self.assertIn('pressed!=(packed&0xffff)', click)

    def test_web_count_is_right_aligned_on_account_line(self):
        draw = section(self.app, 'voidPasswordVault::drawList()', 'voidPasswordVault::draw()')
        for token in ('lv_obj_align(row.title,LV_ALIGN_LEFT_MID,4,0)',
                      'lv_obj_align(row.account,LV_ALIGN_RIGHT_MID,-4,0)',
                      'lv_obj_set_size(row.title,172,21)',
                      'lv_obj_set_style_text_align(row.account,LV_TEXT_ALIGN_RIGHT,0)'):
            self.assertIn(token, draw)

    def test_purpose_rows_are_bounded_single_line_and_visibility_gated(self):
        draw = section(self.app, 'voidPasswordVault::drawList()', 'voidPasswordVault::draw()')
        self.assertLess(draw.index('"名称："'), draw.index('"账号："'))
        self.assertLess(draw.index('"账号："'), draw.index('i<m_listIndex.slots()'))
        for token in ('lv_obj_set_height(name,P::NameHeight)', 'lv_obj_set_height(text,P::NameHeight)',
                      'lv_obj_set_height(row.title,P::NameHeight)', 'lv_obj_set_height(row.account,16)',
                      'Action::PurposePassword,0,P::InputWidth', 'LV_EVENT_PRESSED'):
            self.assertIn(token, draw)
        self.assertNotIn('LV_LABEL_LONG_MODE_WRAP', draw)
        update = section(self.app, 'voidPasswordVault::updateListRows()', 'voidPasswordVault::styleCategory(')
        self.assertIn('m_access.visible(m_webAccount)?entry.password.c_str():"********"', update)
        for forbidden in ('lv_obj_create(', 'c::label(', 'button(', 'push_back(', 'vault::start('):
            self.assertNotIn(forbidden, update)

if __name__ == '__main__':
    unittest.main(verbosity=2)
