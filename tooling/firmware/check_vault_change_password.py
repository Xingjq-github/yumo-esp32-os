"""Device PIN change contracts; source inspection only, never compiles firmware."""
from pathlib import Path
import hashlib,json,unittest
from check_vault_interaction import compact,section
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def prior_change_ui(text):
    """Undo only the explicitly authorized new UI for older section hash checks."""
    pairs=[
      ('  vault::clear(m_oldPin);\n',''),
      ('    const char *prompt = m_page == Page::ChangeOld ? "请输入原密码"\n                         : m_page == Page::ChangeNew ? "设置4到12位新密码"\n                         : m_page == Page::ChangeConfirm ? "再次输入新密码"\n                         : m_page == Page::VerifyReveal ?', '    const char *prompt = m_page == Page::VerifyReveal ?'),
      ('const bool pinPage = m_page == Page::Locked || m_page == Page::VerifyReveal || changingPin();','const bool pinPage = m_page == Page::Locked || m_page == Page::VerifyReveal;'),
      ('auto changeButton = button(header, "修改密码", Action::ChangePassword, 0, 72);\n      lv_obj_set_height(changeButton, L::HeaderHeight);','auto lockButton = button(header, "锁定", Action::Lock, 0, 72);\n      lv_obj_set_height(lockButton, L::HeaderHeight);'),
      ('    if (m_job->operation != vault::Operation::ChangePin)\n      button(m_body, "取消", Action::Cancel);','    button(m_body, "取消", Action::Cancel);'),
      ('if (!m_message.empty() && !pinPage)','if (!m_message.empty() && m_page != Page::Locked && m_page != Page::VerifyReveal)'),
      ('  case Page::ChangeOld:\n  case Page::ChangeNew:\n  case Page::ChangeConfirm:\n    if (m_session)\n      drawPin(Action::SubmitChange);\n    else\n      lock();\n    break;\n','')]
    pairs.extend([
      ('  if (m_job && m_job->operation == vault::Operation::ChangePin) {\n    message("正在保存密码，请稍候");\n    return true;\n  }\n',''),
      ('  if (changingPin()) {\n    finishPinChange("修改失败：操作已取消");\n    return true;\n  }\n','')])
    for new,old in pairs:text=text.replace(new,old)
    return text

class VaultChangePasswordContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=compact((ROOT/'main/apps/password_vault.cpp').read_text(encoding='utf-8'))
        cls.core=compact((ROOT/'main/core/password_vault.cpp').read_text(encoding='utf-8'))
        cls.header=compact((ROOT/'main/core/password_vault.hpp').read_text(encoding='utf-8'))
        cls.submit=section(cls.app,'voidPasswordVault::submitPinChange()','voidPasswordVault::rejectReveal(')
        cls.finish=section(cls.app,'voidPasswordVault::finishPinChange(','voidPasswordVault::submitPinChange(')
        cls.poll=section(cls.app,'voidPasswordVault::poll()','voidPasswordVault::draw()')
    def test_entry_replaces_manual_lock_only(self):
        self.assertNotIn('Action::Lock',self.app)
        self.assertIn('button(header,"修改密码",Action::ChangePassword,0,72)',self.app)
        self.assertIn('sys::EventType::ScreenOff,[this](constsys::Event&){lock();}',self.app)
    def test_begin_requires_session_and_hides_old_reveal(self):
        s=section(self.app,'caseAction::ChangePassword:','caseAction::SubmitChange:')
        for token in ('if(m_session&&m_page==Page::Categories)', 'wipeField()', 'vault::clear(m_pin)', 'vault::clear(m_firstPin)','vault::clear(m_oldPin)','m_reveal.hide()','m_page=Page::ChangeOld'):
            self.assertIn(token,s)
    def test_original_is_async_verified_before_new_page(self):
        self.assertIn('old?vault::Operation::VerifyChangePin:vault::Operation::ChangePin',self.submit)
        self.assertIn('m_oldPin=m_pin;job->master=m_pin;',self.submit)
        s=section(self.poll,'casevault::Operation::VerifyChangePin:','casevault::Operation::ChangePin:')
        self.assertIn('if(m_page==Page::ChangeOld&&m_session)',s);self.assertIn('m_page=Page::ChangeNew',s)
    def test_original_verification_works_with_empty_vault_without_flash(self):
        s=section(self.core,'if(job.operation==Operation::VerifyChangePin||job.operation==Operation::VerifyAccess){','if(job.operation==Operation::ChangePin&&')
        self.assertIn('if(!job.input)',s);self.assertIn('verifyPin(job,*job.input)&&!stopped(job)',s)
        for forbidden in ('entries','load(','commit(','configEncode'):self.assertNotIn(forbidden,s)
    def test_new_is_confirmed_before_final_job(self):
        self.assertIn('if(m_page==Page::ChangeNew){m_firstPin=m_pin;vault::clear(m_pin);m_page=Page::ChangeConfirm;',self.submit)
        self.assertLess(self.submit.index('!old&&m_pin!=m_firstPin'),self.submit.index('std::make_shared'))
        self.assertIn('finishPinChange("修改失败：两次图形不同");return;',self.submit)
    def test_invalid_or_overlong_input_returns_immediately(self):
        self.assertLess(self.submit.index('!vault::pattern::valid'),self.submit.index('std::make_shared'))
        self.assertIn('finishPinChange("请至少连接4个点");return;',self.submit)
        self.assertNotIn('Action::Digit',self.app)
        self.assertIn('pattern::valid',self.app)
        self.assertIn('count > Maximum', (ROOT/'main/core/vault_pattern.hpp').read_text())
    def test_allocation_and_launch_failure_return(self):
        self.assertIn('if(launch(job)!=vault::StartResult::Started)finishPinChange(',self.submit)
        self.assertIn('catch(conststd::bad_alloc&){finishPinChange(',self.submit)
    def test_completion_wipes_all_inputs_and_never_reveals(self):
        for name in ('m_pin','m_firstPin','m_oldPin'):self.assertIn('vault::clear('+name+');',self.finish)
        self.assertIn('if(!changingPin())return;',self.finish)
        self.assertIn('if(!m_session){lock();return;}',self.finish)
        self.assertIn('m_page=Page::Categories;message(text);',self.finish)
        self.assertNotIn('m_reveal.complete',self.finish)
    def test_success_replaces_session_before_return(self):
        s=section(self.poll,'casevault::Operation::ChangePin:','casevault::Operation::Type:')
        self.assertIn('m_page==Page::ChangeConfirm&&m_session&&m_job->output',s)
        self.assertLess(s.index('m_session=std::move(m_job->output)'),s.index('finishPinChange("密码修改成功")'))
    def test_errors_return_and_uncertain_commit_forces_disk_reload(self):
        self.assertIn('(kind==vault::Operation::ChangePin||kind==vault::Operation::Setup)&&m_job->configWriteStarted',self.poll)
        self.assertIn('lock();message("保存结果未确认，请重新解锁核对")',self.poll)
        self.assertIn('kind==vault::Operation::VerifyChangePin||kind==vault::Operation::ChangePin',self.poll)
        self.assertIn('finishPinChange("修改失败：原图形错误或操作未完成")',self.poll)
        self.assertIn('if(!m_job&&m_page==Page::Locked)load();',self.poll)
    def test_pin_screens_keep_existing_fullscreen_geometry(self):
        self.assertIn('constboolpinPage=m_page==Page::Locked||m_page==Page::VerifyReveal||m_page==Page::VerifyAccess||changingPin()',self.app)
        self.assertIn('casePage::ChangeOld:casePage::ChangeNew:casePage::ChangeConfirm:if(m_session)drawPin(Action::SubmitChange)',self.app)
        self.assertIn('!m_message.empty()&&!pinPage',self.app)
    def test_back_cancels_input_but_commit_cannot_be_double_cancelled(self):
        s=section(self.app,'boolPasswordVault::onBack()','vault::StartResultPasswordVault::launch(')
        self.assertIn('m_job->operation==vault::Operation::ChangePin',s)
        self.assertIn('if(changingPin()){finishPinChange("修改失败：操作已取消");returntrue;}',s)
        self.assertIn('if(m_job->operation!=vault::Operation::ChangePin)button(m_body,"取消",Action::Cancel)',self.app)
    def test_lock_detaches_late_jobs_and_wipes_original(self):
        s=section(self.app,'voidPasswordVault::lock()','voidPasswordVault::onHide()')
        for token in ('vault::cancel(m_job);m_job.reset();m_session.reset()', 'vault::clear(m_oldPin)', 'vault::clear(m_firstPin)', 'm_page=Page::Locked', 'lv_obj_clean(root())'):self.assertIn(token,s)
    def test_core_validates_both_pins_and_retains_cooldown(self):
        self.assertIn('job.operation==Operation::VerifyChangePin||job.operation==Operation::VerifyAccess||job.operation==Operation::ChangePin',self.core)
        self.assertIn('if(esp_timer_get_time()<nextUnlock)',self.core)
        self.assertIn('!pattern::valid(job.replacement.data(),job.replacement.size())',self.core)
        self.assertIn('if(!verifyPin(job,*result))return;',self.core)
    def test_persistence_rechecks_disk_and_changes_only_verifier(self):
        s=section(self.core,'elseif(job.operation==Operation::ChangePin){','elseif(job.operation==Operation::Sync||')
        for token in ('bundle=disk.bundle;', 'configDecode(disk.config,*result)', 'verifyPin(job,*result)', 'esp_fill_random(salt,sizeof(salt))', 'derive(job.replacement.c_str()', 'result->pinSalt=', 'result->pinHash='):self.assertIn(token,s)
        for token in ('encryptEmpty','result->url=','result->revision=','result->entries','copyConfig','commit('):self.assertNotIn(token,s)
        self.assertLess(s.index('verifyPin(job,*result)'),s.index('derive(job.replacement'))
    def test_storage_commit_is_single_worker_off_gui(self):
        self.assertIn('job.configWriteStarted=true;if(!commit(disk,bundle,config,job.error,job.cancelled))return;',self.core)
        for token in ('derive(', 'configEncode(', 'commit(', 'esp_partition_'):self.assertNotIn(token,self.app)
        self.assertIn('sys::Power::WorkGuardguard;',self.core)
    def test_both_job_secrets_wiped_on_all_outcomes(self):
        self.assertIn('~Job(){clear(master);clear(replacement);}',self.header)
        self.assertIn('clear(job->replacement);clear(job->master);job->input.reset()',self.core)
    def test_only_four_feature_inputs_and_authorized_build(self):
        base=json.loads((ROOT/'../tooling/firmware/fixtures/contracts/vault-change-password-20260918/before.json').read_text())
        from vault_release_contract import source_before_release,expected_build_hash
        changed={n for n in base['firmware_inputs'] if hashlib.sha256(source_before_release(n)).hexdigest()!=base['files'][n]}
        self.assertEqual(changed,{'main/apps/password_vault.cpp','main/apps/password_vault.hpp','main/core/password_vault.cpp','main/core/password_vault.hpp'})
        for n in base['build_files']+['main/core/vault_storage.cpp','main/core/vault_crypto.cpp','main/core/vault_builtin.hpp','server/password-vault/deployment/local-keys.json']:
            expected=expected_build_hash(n,base['files'][n]) if n.startswith('../artifacts/firmware/') else base['files'][n]
            self.assertEqual(hashlib.sha256((ROOT/n.replace('server/password-vault/','../server/')).read_bytes()).hexdigest(),expected,n)
if __name__=='__main__':unittest.main(verbosity=2)
