"""Pattern auth integration: no device access or test exclusion changes."""
from pathlib import Path
import unittest
from check_vault_interaction import compact, section

ROOT=Path(__file__).resolve().parents[2]/'firmware'
def read(name): return (ROOT/name).read_text(encoding='utf-8')

class PatternContracts(unittest.TestCase):
    def test_all_auth_paths_use_the_same_pattern_validator_and_verifier(self):
        worker=compact(read('main/core/password_vault.cpp'))
        gate=section(worker,'if(job.operation==Operation::Setup||','Diskdisk;')
        for operation in ('Setup','Unlock','WebUnlock','VerifyPin','VerifyAccess','VerifyChangePin','ChangePin'):
            self.assertIn('Operation::'+operation,gate)
        self.assertIn('pattern::valid(job.master.data(),job.master.size())',gate)
        self.assertIn('pattern::valid(job.replacement.data(),job.replacement.size())',gate)
        verify=section(worker,'boolverifyPin(', 'voidcopyConfig(')
        self.assertLess(verify.index('!session.patternAuth'),verify.index('derive('))
        for token in ('equal(', 'nextUnlock=', 'failures=std::min', 'job.cancelled'):
            self.assertIn(token,verify)
    def test_load_is_read_only_and_migration_requires_authenticated_legacy_config(self):
        worker=compact(read('main/core/password_vault.cpp'))
        load=section(worker,'if(job.operation==Operation::Load){','//Stablestoragekey')
        self.assertIn('configDecode(disk.config,configSession)',load)
        self.assertIn('job.needsPatternSetup=!configSession.patternAuth',load)
        self.assertNotIn('commit(',load)
        setup=section(worker,'if(job.operation==Operation::Setup){','elseif(job.operation==Operation::Unlock||')
        for token in ('bundle=disk.bundle','configDecode(disk.config,*result)','if(result->patternAuth)',
                      'if(!job.blank)', 'result->patternAuth=true'):
            self.assertIn(token,setup)
        self.assertLess(setup.index('if(result->patternAuth)'),setup.index('derive('))
        self.assertEqual(worker.count('commit(disk,bundle,config,job.error,job.cancelled)'),1)
    def test_pattern_marker_is_encrypted_and_unknown_auth_fails_closed(self):
        config=compact(read('main/core/vault_json.cpp'))
        decode=section(config,'boolconfigDecode(', 'boolconfigEncode(')
        self.assertIn('std::strcmp(auth->valuestring,"pattern-v1")',decode)
        self.assertIn('session.patternAuth=auth!=nullptr',decode)
        encode=config[config.index('boolconfigEncode('):]
        self.assertIn('if(!session.patternAuth)returnfalse',encode)
        self.assertIn('cJSON_AddStringToObject(doc,"auth","pattern-v1")',encode)
        self.assertIn('gcm(true,session.key',encode)
        worker=compact(read('main/core/password_vault.cpp'))
        self.assertIn('to.patternAuth=from.patternAuth',worker)
    def test_first_setup_confirms_and_uncertain_commit_reloads(self):
        app=compact(read('main/apps/password_vault.cpp'))
        setup=section(app,'caseAction::Unlock:','caseAction::Category:')
        self.assertIn('m_needsPatternSetup&&m_firstPin.empty()',setup)
        self.assertIn('m_needsPatternSetup&&m_firstPin!=m_pin',setup)
        self.assertLess(setup.index('m_firstPin!=m_pin'),setup.index('std::make_shared'))
        self.assertIn('m_needsPatternSetup?vault::Operation::Setup:vault::Operation::Unlock',setup)
        self.assertIn('(kind==vault::Operation::ChangePin||kind==vault::Operation::Setup)&&m_job->configWriteStarted',app)
        self.assertIn('m_needsPatternSetup=m_job->needsPatternSetup',app)
    def test_touch_stays_local_and_submits_once_after_release(self):
        view=compact(read('main/ui/vault_pattern.cpp'))
        for token in ('lv_obj_set_press_lock(board,true)','lv_obj_set_gesture_bubble(board,false)',
                      'LV_EVENT_PRESS_LOST','LV_EVENT_INDEV_RESET','LV_EVENT_RELEASED',
                      'self->enabled=false','vault::wipe(encoded,sizeof(encoded))'):
            self.assertIn(token,view)
        event=section(view,'voidVaultPattern::event(', 'voidVaultPattern::paint(')
        for forbidden in ('lv_obj_create(', 'lv_obj_clean(', 'derive(', 'commit(', 'new '):
            self.assertNotIn(forbidden,event)
        app=compact(read('main/apps/password_vault.cpp'))
        self.assertIn('if(m_redraw&&!m_pattern.drawing())draw()',app)
        self.assertIn('m_patternPending=true',app)
        header=compact(read('main/apps/password_vault.hpp'))
        self.assertIn('allowsQuickPanel()constoverride{return!patternPage();}',header)
    def test_hint_is_translucent_centered_timed_and_does_not_capture_touch(self):
        view=compact(read('main/ui/vault_pattern.cpp'))
        for token in ('lv_obj_center(toast)','lv_obj_set_style_bg_opa(toast,LV_OPA_80,0)',
                      'lv_tick_elaps(hintAt)>=1800','lv_obj_set_clickable(toast,false)',
                      'lv_obj_set_clickable(label,false)'):
            self.assertIn(token,view)
    def test_browser_uses_pattern_and_cannot_set_or_bypass_device_auth(self):
        web=read('main/core/lan_web.html');server=compact(read('main/core/lan_web.cpp'))
        for old in ('id="pin"','inputmode="numeric"','maxlength="12"'):
            self.assertNotIn(old,web)
        for token in ('getCoalescedEvents','setPointerCapture','onpointercancel','requestAnimationFrame',
                      "body:pattern",'patternCells.length<4'):
            self.assertIn(token,web)
        login=section(server,'esp_err_tlogin(', 'esp_err_tpasswords(')
        self.assertIn('pattern::valid(pin.data(),pin.size())',login)
        self.assertIn('vault::Operation::WebUnlock',login)
        self.assertNotIn('Operation::Setup',login)
    def test_lock_wipes_pending_stroke_and_input_state(self):
        app=compact(read('main/apps/password_vault.cpp'))
        lock=section(app,'voidPasswordVault::lock()', 'voidPasswordVault::onHide()')
        for token in ('m_pattern.detach()','m_patternPending=false','vault::clear(m_pin)',
                      'vault::clear(m_oldPin)','vault::clear(m_firstPin)','vault::cancel(m_job)'):
            self.assertIn(token,lock)
        self.assertLess(lock.index('m_pattern.detach()'),lock.index('lv_obj_clean(root())'))

if __name__=='__main__': unittest.main(verbosity=2)
