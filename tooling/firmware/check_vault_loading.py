"""Source bindings for vault loading regression; C++ model tests production state.
No device, real vault data, server or flash access.
"""
from pathlib import Path
import re
import unittest
ROOT = (Path(__file__).resolve().parents[2]/'firmware')
def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')
def compact(text):
    return re.sub(r'\s+', '', text)
class VaultLoadingContracts(unittest.TestCase):
    def test_psram_worker_stack_preserves_crypto_headroom(self):
        code = compact(read('main/core/password_vault.cpp'))
        start = code[code.index('StartResultstart('):code.index('boolidle()')]
        self.assertIn('xTaskCreateWithCaps(worker,"vault_job",12288,argument,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)', start)
        self.assertNotIn('MALLOC_CAP_INTERNAL', start)
        self.assertIn('vTaskDeleteWithCaps(nullptr)', code)
    def test_xip_and_external_stack_guard_match_actual_configuration(self):
        code = read('main/core/password_vault.cpp')
        config = read('sdkconfig')
        for name in ('CONFIG_SPIRAM_FETCH_INSTRUCTIONS', 'CONFIG_SPIRAM_RODATA', 'CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY'):
            self.assertIn('!' + name, code)
            self.assertIn(name + '=y', config)
        self.assertIn('#error "Vault PSRAM stack requires', code)
    def test_start_failure_is_typed_and_releases_worker_slot(self):
        code = compact(read('main/core/password_vault.cpp'))
        start = code[code.index('StartResultstart('):code.index('boolidle()')]
        for state in ('Started', 'Busy', 'Unavailable', 'NoMemory'):
            self.assertIn('returnStartResult::' + state, start)
        self.assertIn('if(!argument){active.store(false);returnStartResult::NoMemory;}', start)
        self.assertIn('deleteargument;active.store(false);returnStartResult::NoMemory;', start)
        self.assertIn('!sys::BleHid::privateInputIdle()||active.exchange(true)', start)
    def test_production_state_machine_used_by_app(self):
        app = compact(read('main/apps/password_vault.cpp'))
        self.assertIn('vault::LoadStatem_load;', compact(read('main/apps/password_vault.hpp')))
        self.assertNotIn('m_loaded', app)
        self.assertIn('m_load.attempted(launch(std::make_shared<vault::Job>(vault::Operation::Load)),now)', app)
        self.assertIn('#include "core/vault_load_state.hpp"', read('../tooling/firmware/vault_load_state_test.cpp'))
    def test_busy_path_allocates_no_jobs_or_repeated_widgets(self):
        code = compact(read('main/apps/password_vault.cpp'))
        load = code[code.index('voidPasswordVault::load()'):code.index('voidPasswordVault::timer(')]
        self.assertLess(load.index('m_load.expired(now)'), load.index('!vault::idle()'))
        self.assertLess(load.index('!vault::idle()'), load.index('std::make_shared'))
        self.assertIn('previous!=vault::LoadState::Phase::Waiting', load)
        self.assertIn('if(m_job||!m_load.due(now))return;', load)
        self.assertIn('catch(conststd::bad_alloc&)', load)
    def test_failed_read_is_not_marked_ready_or_blank(self):
        code = compact(read('main/apps/password_vault.cpp'))
        self.assertIn('if(kind==vault::Operation::Load){m_load.finish(false);m_exists=m_blank=false;}', code)
        self.assertIn('message(m_job->error.empty()?"操作已取消":m_job->error.c_str())', code)
    def test_failed_load_shows_error_retry_not_reset(self):
        code = compact(read('main/apps/password_vault.cpp'))
        self.assertIn('wrap(m_body,m_message.empty()?"正在读取密码区":m_message.c_str())', code)
        self.assertIn('button(m_body,"重新读取",Action::RetryLoad)', code)
        # Reset has now been removed entirely, including during failed loads.
        self.assertNotIn('Action::Erase', code)
        self.assertNotIn('Operation::Erase', code)
        self.assertIn('if(!m_load.ready()||(!m_exists&&!m_blank))return;', code)
    def test_reopen_and_lock_reset_the_load_state(self):
        code = compact(read('main/apps/password_vault.cpp'))
        self.assertIn('m_load.reset(lv_tick_get());load();draw();', code)
        self.assertIn('m_load.reset(lv_tick_get());m_exists=m_blank=m_needsPatternSetup=false;', code)
        self.assertIn('if(!m_job&&m_page==Page::Locked)load();', code)
    def test_storage_io_errors_not_treated_as_invalid_header(self):
        code = compact(read('main/core/vault_storage.cpp'))
        self.assertIn('if(io.read(p,offset,h.data(),h.size())!=ESP_OK)returnSlotResult::ReadError;', code)
        self.assertIn('if(io.read(p,offset+Sector,data.data(),data.size())!=ESP_OK)returnSlotResult::ReadError;', code)
        self.assertLess(code.index('if(ar==SlotResult::ReadError||br==SlotResult::ReadError)'), code.index('if(av||bv)'))
        self.assertLess(code.index('if(ar==SlotResult::ReadError||br==SlotResult::ReadError)'), code.index('disk.blank=true;'))
    def test_no_load_retry_writes_and_commits_still_header_last(self):
        code = compact(read('main/core/vault_storage.cpp'))
        load = code[code.index('boolload('):code.index('boolcommit(')]
        self.assertNotIn('esp_partition_write', load)
        self.assertNotIn('esp_partition_erase', load)
        self.assertIn('!previous.blank&&previous.slot<0', code)
        self.assertLess(code.index('io.write(p,offset+Sector+i'), code.index('io.write(p,offset,h.data()'))
    def test_crypto_failure_never_selects_an_older_slot_or_marks_data_blank(self):
        code = compact(read('main/core/vault_storage.cpp'))
        self.assertIn('if(!hash(h.data(),64,digest))returnSlotResult::VerifyError;', code)
        self.assertIn('if(!hash(data.data(),data.size(),digest))returnSlotResult::VerifyError;', code)
        failure = code.index('if(ar==SlotResult::VerifyError||br==SlotResult::VerifyError)')
        self.assertLess(failure, code.index('if(av||bv)'))
        self.assertLess(failure, code.index('disk.blank=true;'))
        self.assertIn('error="密码区校验未完成，请重试";returnfalse;', code)
        # Genuine digest mismatches must still be rejected, not accepted as an
        # infrastructure failure or bypassed to make the UI open.
        self.assertIn('if(!equal(digest,h.data()+64,32))returnSlotResult::Invalid;', code)
        self.assertIn('if(!equal(digest,h.data()+32,32))returnSlotResult::Invalid;', code)
        self.assertIn('if(!framing(data.data(),b))returnSlotResult::Invalid;', code)
    def test_hash_is_allocation_free_and_independent_of_dma_or_psa(self):
        code = compact(read('main/core/vault_crypto.cpp'))
        body = code[code.index('boolhash('):code.index('boolderive(')]
        self.assertNotIn('psa_', body)
        self.assertNotIn('malloc', body)
        self.assertIn('vault_sha256_statestate{};', body)
        self.assertIn('std::min(size-offset,size_t{4096})', body)
        self.assertIn('vault_sha256_process(&state,input+offset,chunk)', body)
        self.assertIn('vault_sha256_done(&state,output)', body)
        self.assertIn('wipe(&state,sizeof(state));', body)
        self.assertIn('if(!ok)wipe(output,32);', body)
    def test_single_worker_and_private_typing_cancellation_preserved(self):
        code = compact(read('main/core/password_vault.cpp'))
        self.assertIn('boolidle(){return!active.load()&&sys::BleHid::privateInputIdle();}', code)
        self.assertIn('clear(job->master);job->input.reset();', code)
        self.assertIn('job->output.reset();', code)
        self.assertIn('sys::BleHid::cancelPrivateInput();', code)
        app = compact(read('main/apps/password_vault.cpp'))
        self.assertIn('if(launch(job)!=vault::StartResult::Started)sys::BleHid::cancelPrivateInput();', app)
if __name__ == '__main__':
    unittest.main(verbosity=2)
