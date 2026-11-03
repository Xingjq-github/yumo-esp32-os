"""Fullscreen PIN, explicit Enter and vault BLE UI contracts. No compiler/device/network I/O."""
from pathlib import Path
import ast
import hashlib
import json
import re
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/vault-fullscreen-enter-20260918'
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(text):return re.sub(r'\s+','',text)
def section(text,start,end=None):
    a=text.index(start)
    return text[a:text.index(end,a+len(start))] if end else text[a:]
def constants(name):
    body=section(read('main/core/vault_interaction.hpp'),'struct '+name+' {','};')
    values={}
    def evaluate(node):
        if isinstance(node,ast.Constant) and type(node.value) is int:return node.value
        if isinstance(node,ast.Name):return values[node.id]
        if isinstance(node,ast.BinOp):
            a,b=evaluate(node.left),evaluate(node.right)
            if isinstance(node.op,ast.Add):return a+b
            if isinstance(node.op,ast.Sub):return a-b
            if isinstance(node.op,ast.Mult):return a*b
            if isinstance(node.op,ast.Div):
                assert a>=0 and b>0
                return a//b # These layout constants use positive C++ integer arithmetic.
        raise AssertionError(ast.dump(node))
    for declaration in re.findall(r'static constexpr int ([^;]+);',body):
        for term in declaration.split(','):
            key,expression=term.split('=',1)
            values[key.strip()]=evaluate(ast.parse(expression.strip(),mode='eval').body)
    return values
class FullscreenEnterContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=compact(read('main/apps/password_vault.cpp'))
        cls.core=compact(read('main/core/password_vault.cpp'))
    def test_pin_pages_omit_header_and_take_entire_240_pixel_height(self):
        s=section(self.app,'voidPasswordVault::draw()')
        self.assertIn('constboolpinPage=m_page==Page::Locked||m_page==Page::VerifyReveal||m_page==Page::VerifyAccess||changingPin();',s)
        self.assertIn('if(!pinPage){autoheader=c::panel(root(),L::Width,L::HeaderHeight)',s)
        self.assertIn('m_body=c::panel(root(),L::Width,pinPage?L::BodyHeight:L::NormalBodyHeight,!pinPage)',s)
        self.assertLess(s.index('constboolpinPage'),s.index('if(m_job)'))
    def test_nonpin_navigation_and_hardware_back_are_preserved(self):
        s=section(self.app,'voidPasswordVault::draw()','if(m_job)')
        self.assertIn('button(header,LV_SYMBOL_LEFT,Action::Back',s)
        self.assertIn('if(m_session&&m_page==Page::Categories){autochangeButton=button(header,"修改密码",Action::ChangePassword',s)
        s=section(self.app,'boolPasswordVault::onBack()','vault::StartResultPasswordVault::launch(')
        self.assertIn('m_page==Page::VerifyReveal',s)
        self.assertIn('m_page==Page::Detail?Page::List:Page::Detail',s)
        self.assertIn('returnfalse;',s)
    def test_numeric_keys_use_real_fullscreen_geometry(self):
        p=constants('PinLayout')
        self.assertEqual((p['Width'],p['BodyHeight'],p['KeyWidth'],p['KeyHeight']),(240,240,79,53))
        self.assertEqual(p['ContentHeight'],237)
        self.assertLessEqual(p['ContentHeight'],p['BodyHeight'])
        self.assertEqual(p['HeaderHeight']+p['Gap']+p['NormalBodyHeight'],240)
        self.assertLessEqual(3*p['KeyWidth']+2*p['Gap'],p['Width'])
    def test_account_password_width_is_exactly_three_quarters(self):
        p=constants('InputLayout')
        self.assertEqual((p['Width'],p['TextWidth'],p['EnterWidth'],p['RowHeight'],p['Height']),(240,180,59,40,81))
        self.assertEqual(4*p['TextWidth'],3*p['Width'])
        self.assertEqual(p['TextWidth']+p['Gap']+p['EnterWidth'],p['Width'])
        self.assertEqual(2*p['RowHeight']+p['Gap'],p['Height'])
    def test_production_buttons_use_one_tall_enter_on_the_right(self):
        s=section(self.app,'casePage::Detail:','casePage::Qr:')
        # The ordinary password layout follows the separate code-only branch.
        code_start=s.index('if(entry.category=="other"){')
        code_end=s.index('}else{',code_start)+len('}')
        code=s[code_start:code_end]
        self.assertEqual(code.count('Action::Enter'),1)
        self.assertNotIn('Action::User',code)
        s=s[:code_start]+s[code_end:]
        for token in ('c::panel(m_body,I::Width,I::Height)',
                      'lv_obj_set_flex_flow(actions,LV_FLEX_FLOW_ROW)',
                      'c::panel(actions,I::TextWidth,I::Height)',
                      'lv_obj_set_flex_flow(inputs,LV_FLEX_FLOW_COLUMN)',
                      'button(inputs,entry.username.empty()?"未设置账号":entry.username.c_str(),Action::User,0,I::TextWidth)',
                      'button(inputs,(m_access.super()||m_reveal.visible(m_selected))?entry.password.c_str():"********",Action::Password,0,I::TextWidth)',
                      'button(actions,"回车",Action::Enter,0,I::EnterWidth)',
                      'lv_obj_set_height(enter,I::Height)',
                      'c::disabled(account,entry.username.empty())'):
            self.assertIn(token,s)
        self.assertEqual(s.count('Action::Enter'),1)
    def test_device_reset_page_action_operation_and_erase_api_are_removed(self):
        for name in ('main/apps/password_vault.hpp','main/apps/password_vault.cpp',
                     'main/core/password_vault.hpp','main/core/password_vault.cpp',
                     'main/core/vault_storage.hpp','main/core/vault_storage.cpp'):
            with self.subTest(file=name):
                s=read(name)
                self.assertNotRegex(s,r'\bErase(?:Confirm)?\b|\b(?:vault::)?erase\s*\(')
                for text in ('重置','确认清空1MiB','请先明确清空','初始化中断，请再次初始化'):
                    self.assertNotIn(text,s)
        self.assertIn('原数据已保留',self.app)
    def test_atomic_inactive_slot_commit_erasure_is_still_present(self):
        s=compact(read('main/core/vault_storage.cpp'))
        self.assertEqual(s.count('esp_partition_erase_range('),1)
        commit=section(s,'boolcommit(')
        for token in ('target=previous.slot==0?1:0','offset=target*SlotSize',
                      'i<SlotSize','esp_partition_erase_range(p,offset+i,Sector)',
                      'io.write(p,offset,h.data(),h.size())'):
            self.assertIn(token,commit)
    def test_enter_action_requires_detail_and_private_current_link(self):
        s=section(self.app,'caseAction::Enter:','caseAction::Settings:')
        self.assertIn('if(m_page==Page::Detail||purposePage())startTyping(false,sys::BleHid::privateLinkGeneration(),true)',s)
        s=section(self.app,'voidPasswordVault::startTyping(','voidPasswordVault::requestBluetooth(')
        for token in ('enter?vault::Operation::Enter:vault::Operation::Type',
                      '!m_session||m_selected>=m_session->entries.size()',
                      '!link||link!=sys::BleHid::privateLinkGeneration()',
                      'job->permit=sys::BleHid::beginPrivateInput()',
                      'if(launch(job)!=vault::StartResult::Started)sys::BleHid::cancelPrivateInput()'):
            self.assertIn(token,s)
    def test_enter_is_one_guarded_press_release_not_unprotected_send(self):
        s=section(self.core,'boolenter(','booltype(')
        for token in ('!job.input','job.entry>=job.input->entries.size()',
                      '!job.link','!job.permit','stopped(job)',
                      'sys::BleHid::privateLinkGeneration()!=job.link',
                      'sys::BleHid::sendPrivateKey(0x28,0,job.link,job.permit)!=ESP_OK',
                      'job.progress.store(1)'):
            self.assertIn(token,s)
        self.assertEqual(s.count('sendPrivateKey('),1)
        self.assertNotIn('sendKey(',s)
        self.assertLess(s.index('stopped(job)'),s.index('sendPrivateKey('))
        self.assertLess(s.index('sendPrivateKey('),s.index('job.progress.store(1)'))
        # The existing keyboard model uses the same Return usage.
        self.assertRegex(read('main/apps/hid_keyboard_model.hpp'),r'(?s)(?:回车|Enter).{0,80}0x28|0x28.{0,80}(?:回车|Enter)')
    def test_enter_job_runs_without_flash_loading(self):
        s=section(self.core,'voidrun(','voidworker(')
        token='if(job.operation==Operation::Enter){job.ok=enter(job);return;}'
        self.assertIn(token,s)
        self.assertLess(s.index(token),s.index('Diskdisk'))
    def test_credentials_never_implicitly_append_enter(self):
        s=section(self.core,'booltype(','std::stringhex(')
        self.assertNotIn('enter(',s)
        self.assertNotIn('0x28',s)
        self.assertEqual(s.count('sendPrivateKey('),1)
        self.assertIn('!printable(text.data(),text.size())',s)
        self.assertIn('if(i+1<text.size())vTaskDelay(pdMS_TO_TICKS(interaction::KeyGapMs))',s)
    def test_worker_session_cleanup_and_storage_pin_text_unchanged(self):
        for item in json.loads((RUN/'preserved_vault_sections.json').read_text()):
            with self.subTest(file=item['file'],section=item['start']):
                actual=section(read(item['file']),item['start'],item['end'])
                if item['start']=='std::string hex(':
                    current=json.loads((ROOT/'../tooling/firmware/fixtures/contracts/vault-pattern-20261028/sections.json').read_text())
                    self.assertEqual(hashlib.sha256(actual.encode()).hexdigest(),current[item['start']]['sha256'])
                    continue
                # Browser unlock alone may run with the screen dark. Normalize
                # exactly that gate; preserve device-worker cancellation bytes.
                actual=actual.replace('(!sys::Power::isScreenOn() && job.operation != Operation::WebUnlock)', '!sys::Power::isScreenOn()')
                actual=actual.replace('(!sys::Power::isScreenOn() && job->operation != Operation::WebUnlock)', '!sys::Power::isScreenOn()')
                actual=actual.replace('if (stopped(*job)) {', 'if (job->cancelled.load() || !sys::Power::isScreenOn() ||\n        sys::Power::sleepPending()) {')
                if item['file']=='main/core/vault_storage.cpp':
                    # Storage verification now distinguishes crypto failure
                    # from corrupt data. Preserve the write/selection guards
                    # instead of freezing the old combined error expression.
                    storage=compact(actual)
                    loading=section(storage,'boolload(','boolcommit(')
                    self.assertNotIn('esp_partition_write',loading)
                    self.assertNotIn('esp_partition_erase',loading)
                    for result in ('ReadError','VerifyError'):
                        self.assertLess(loading.index('if(ar==SlotResult::'+result),loading.index('if(av||bv)'))
                    for token in ('p->address==0xEC0000','p->size==0x100000',
                                  '!previous.blank&&previous.slot<0',
                                  'previous.generation==UINT32_MAX'):
                        self.assertIn(token,storage)
                    self.assertLess(storage.index('io.write(p,offset+Sector+i'),storage.index('io.write(p,offset,h.data()'))
                    continue
                # Sync authentication now has a separate error; freeze the rest of
                # this section while allowing only the clarified decode failure text.
                if item['file']=='main/core/password_vault.cpp' and item['start']=='bool stopped(':
                    self.assertIn('密码文件校验失败，原数据未修改',actual)
                    actual=actual.replace('密码文件校验失败，原数据未修改','内置密钥不匹配或密码文件损坏')
                # Authorized PIN replacement adds a second secret to cleanup.
                if item['start']=='void worker(':
                    self.assertIn("      run(*job);",actual)
                    self.assertIn('    clear(job->replacement);\n',actual)
                    actual=actual.replace('    clear(job->replacement);\n','')
                    # Admission capture prevents delayed SD fallback from reopening
                    # an ejected card; crypto/PIN/worker cleanup remains unchanged.
                    self.assertIn('  job->sdAccess=core::sd::requestAccess(true);\n',actual)
                    actual=actual.replace('  job->sdAccess=core::sd::requestAccess(true);\n','')
                self.assertEqual(hashlib.sha256(actual.encode()).hexdigest(),item['sha256'])
    def test_enter_success_and_failure_return_to_detail(self):
        s=section(self.app,'voidPasswordVault::poll()','voidPasswordVault::draw()')
        self.assertIn('casevault::Operation::Enter:m_page=m_typePage;',s)
        self.assertIn('kind==vault::Operation::Type||kind==vault::Operation::Enter',s)
        self.assertIn('m_page=m_session?m_typePage:Page::Locked',s)
        self.assertIn('message("回车已发送")',s)
    def test_network_requests_use_shared_service_and_stable_saved_identity(self):
        s=section(self.app,'voidPasswordVault::requestBluetooth(','boolPasswordVault::patternPage(')
        self.assertIn('if(m_page!=Page::Network)return;',s)
        self.assertIn('if(index>=m_bonds.size())return;',s)
        self.assertIn('sys::SystemService::reconnectBond(m_bonds[index])',s)
        self.assertIn('sys::SystemService::request(service,!discovering)',s)
        self.assertNotIn('BleHid::reconnectBond(',s)
        self.assertNotIn('BleHid::makeDiscoverable(',s)
    def test_queue_acceptance_is_not_reported_as_connection_success(self):
        s=section(self.app,'voidPasswordVault::requestBluetooth(','boolPasswordVault::patternPage(')
        self.assertLess(s.index('constautorevision='),s.index('result=sys::SystemService::'))
        self.assertIn('if(result!=ESP_OK)',s)
        self.assertIn('m_btRevision=revision;m_btAction=service;m_btPending=true;',s)
        self.assertIn('蓝牙请求已提交，正在处理',s)
        self.assertNotIn('已连接',s)
    def test_completion_uses_changed_revision_and_matching_action_for_errors(self):
        s=section(self.app,'voidPasswordVault::poll()','if(m_redraw&&!m_pattern.drawing())')
        for token in ('m_btPending&&!sys::SystemService::busy(sys::ServiceDomain::Bluetooth)',
                      'if(result.revision!=m_btRevision)',
                      'm_btPending=false;m_message.clear()',
                      'if(result.action==m_btAction&&result.error!=ESP_OK)',
                      'esp_err_to_name(result.error)'):
            self.assertIn(token,s)
    def test_bond_list_refreshes_on_revision_not_every_poll(self):
        s=section(self.app,'voidPasswordVault::poll()','if(m_redraw&&!m_pattern.drawing())')
        self.assertIn('if(state.bondRevision!=m_bondRevision)m_redraw=true;',s)
        s=section(self.app,'casePage::Network:')
        self.assertIn('m_bondRevision=state.bondRevision;',s)
        self.assertIn('if(state.bonds[i].keyboard)continue;',s)
        self.assertIn('m_bondButtons.push_back(row)',s)
    def test_live_status_uses_actual_connection_and_retains_name_address(self):
        s=section(self.app,'if(m_page==Page::Network&&!m_job&&m_status)','voidPasswordVault::draw()')
        for token in ('connected=b.state==sys::BleHidState::Connected',
                      'elseif(b.connecting())',
                      '蓝牙：等待目标设备连接',
                      'showPeer=connected||b.connecting()',
                      'shownPeer=b.connecting()?b.target:b.peer',
                      'c::setText(m_peerAddress,showPeer?address:"")',
                      'for(autorow:m_bondButtons)c::disabled(row,!ready)',
                      'canCancel?"取消连接":"断开蓝牙"',
                      'c::disabled(m_disconnectButton,busy||(!connected&&!canCancel))'):
            self.assertIn(token,s)
    def test_lock_cancels_owned_invitation_and_clears_deleted_widgets(self):
        s=section(self.app,'voidPasswordVault::lock()','voidPasswordVault::onHide()')
        self.assertIn('if(m_pairStarted)sys::SystemService::cancelBluetoothConnection()',s)
        self.assertNotIn('SystemService::request(',s)
        for token in ('m_btPending=false', 'm_status=m_peerName=m_peerAddress=nullptr',
                      'm_bleButton=m_pairButton=m_disconnectButton=nullptr','m_bondButtons.clear()'):
            self.assertIn(token,s)
            self.assertLess(s.index(token),s.index('lv_obj_clean(root())'))
        s=section(self.app,'voidPasswordVault::draw()','lv_obj_clean(root())')
        self.assertIn('m_bondButtons.clear()',s)
    def test_only_seven_authorized_production_files_changed_from_baseline(self):
        before=json.loads((RUN/'before.json').read_text())['files']
        allowed={'main/apps/password_vault.cpp','main/apps/password_vault.hpp',
                 'main/core/password_vault.cpp','main/core/password_vault.hpp',
                 'main/core/vault_storage.cpp','main/core/vault_storage.hpp','main/core/vault_interaction.hpp'}
        changed=set()
        for name,item in before.items():
            if not (name.startswith(('main/','server/')) or name in ('sdkconfig','sdkconfig.defaults','partitions.csv','flash_only.bat','build_flash.bat')):continue
            from vault_release_contract import source_before_release
            raw=source_before_release(name)
            if name in ('main/main.cpp','main/apps/settings_system.cpp'):
                self.assertEqual(raw.count(b'VAULT-ENTER-20260918'),1)
                raw=raw.replace(b'VAULT-ENTER-20260918',b'INPUT-UI-20260918')
            actual=hashlib.sha256(raw).hexdigest()
            if actual!=item['sha256']:changed.add(name)
        self.assertEqual(changed,allowed)
    def test_build_artifacts_match_authorized_release_or_prebuild_baseline(self):
        before=json.loads((RUN/'before.json').read_text())['files']
        protected=[name for name in before if name.startswith('build/')]
        self.assertGreaterEqual(len(protected),7)
        # The user subsequently authorized build/flash. Only a validated build
        # receipt supersedes the old freeze; this is not an exclusion.
        receipt=ROOT/'../tooling/firmware/fixtures/contracts/vault-enter-build-flash-20260918-1507/build_receipt.json'
        expected=before
        if receipt.exists():
            release=json.loads(receipt.read_text(encoding='utf-8'))
            self.assertEqual(release['marker'],'VAULT-ENTER-20260918')
            self.assertEqual(release['build']['status'],'succeeded')
            expected=release['generated_build_files']
            self.assertTrue(set(protected)<=set(expected))
        from vault_release_contract import expected_build_hash
        for name in protected:
            path=ROOT.parent/'artifacts/firmware'/Path(name).relative_to('build')
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),expected_build_hash(name,expected[name]['sha256']),name)
if __name__=='__main__':unittest.main(verbosity=2)
