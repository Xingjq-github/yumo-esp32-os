"""Source-only sync/storage separation contracts. Never outputs secrets."""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8-sig')
def compact(s):return re.sub(r'\s+','',s)
class SyncKeyContracts(unittest.TestCase):
    def test_compiled_keys_match_without_printing_values(self):
        def configured_key(path):
            lines=[s.strip() for s in read(path).splitlines() if s.strip() and not s.strip().startswith('#')]
            self.assertTrue(len(lines)==1,'Expected one sync configuration entry')
            match=re.fullmatch(r'sync\.key\.hex\s*=\s*([0-9a-fA-F]{64})',lines[0])
            self.assertTrue(match is not None,'Sync configuration requires 64 hexadecimal characters')
            return bytes.fromhex(match[1])
        cpp=configured_key('vault-sync-key.properties')
        java=configured_key('../server/vault-sync-key.properties')
        self.assertTrue(len(cpp)==32 and cpp==java,'Compiled sync identities must match')
        self.assertIn('vault_sync_key_generated.hpp',read('main/core/vault_deploy_key.hpp'))
        self.assertIn('vault-sync-key.properties',read('main/CMakeLists.txt'))
        self.assertIn('vault-sync-key.properties',read('../server/pom.xml'))
        for p in ('main/core/vault_sync.cpp','../server/src/main/java/com/joyvault/BuiltinSyncKey.java'):
            self.assertIn('JYVAULT-DEPLOY-KEY-V2',read(p))
        self.assertIn('sizeof(domain) - 1',read('main/core/vault_sync.cpp'))
    def test_stable_storage_identity_is_not_rotated(self):
        raw=(ROOT/'main/core/vault_builtin.hpp').read_bytes()
        identity=json.loads(read('../server/deployment/local-keys.json'))
        header=raw.decode('utf-8')
        for name,field in [('Key','key'),('Id','vault_id'),('Salt','salt')]:
            match=re.search(r'uint8_t '+name+r'\[\] = \{([^}]+)\}',header)
            self.assertTrue(match is not None,'Missing storage identity field')
            actual=bytes(int(x.strip(),16) for x in match[1].split(','))
            self.assertTrue(actual==bytes.fromhex(identity[field]),'Storage identities must match')
        for p in ('main/core/password_vault.cpp','main/core/vault_crypto.cpp','main/core/vault_json.cpp','main/core/vault_storage.cpp'):
            self.assertNotIn('DeployKey',read(p));self.assertNotIn('vault_deploy_key',read(p))
        self.assertIn('std::memcpy(key.bytes.data(), builtin::Key, 32)',read('main/core/password_vault.cpp'))
        self.assertNotIn('BuiltinSyncKey',read('../server/src/main/java/com/joyvault/VaultProtocol.java'))
        self.assertNotIn('BuiltinSyncKey',read('../server/src/main/java/com/joyvault/VaultStore.java'))
    def test_runtime_settings_cannot_disable_authentication(self):
        auth=read('../server/src/main/java/com/joyvault/AuthService.java')
        device=auth.split('void device(',1)[1].split('private ResponseEntity',1)[0]
        self.assertIn('BuiltinSyncKey.proof()',device);self.assertIn('new ApiException(403',device)
        self.assertNotIn('expected!=null',device)
        self.assertNotIn('deploy_key',read('../server/src/main/java/com/joyvault/DeploymentKeys.java'))
        cfg=read('../tooling/firmware/configure_vault.py')
        self.assertNotIn("array('DeployKey'",cfg)
    def test_verification_precedes_body_acceptance(self):
        text=read('main/core/vault_sync.cpp')
        self.assertIn('"X-Vault-Key", proof.value',text)
        self.assertIn('proof.received == 1',text)
        self.assertLess(text.index('if (!proof.matched'),text.index('esp_http_client_read(client'))
        self.assertIn('disable_auto_redirect = true',text)
        self.assertIn('~OutputGuard()',text)
        self.assertIn('outputGuard.keep = ok',text)
        controller=read('../server/src/main/java/com/joyvault/VaultController.java')
        self.assertIn('auth.device(r);VaultStore.Snapshot row=store.current()',controller)
        self.assertIn('response.setHeader("X-Vault-Key",BuiltinSyncKey.proof())',controller)
    def test_failed_primary_message_survives_missing_sd_config(self):
        text=read('main/core/password_vault.cpp')
        self.assertIn('const std::string primaryError = job.error',text)
        self.assertIn('primaryError+"\\nSD: "+job.error',text)
    def test_software_gcm_is_built_with_raii_and_cooperative_yields(self):
        crypto=read('main/core/vault_crypto.cpp')
        self.assertIn('~GcmContext()',crypto);self.assertIn('vault_gcm_aes256_wipe',crypto)
        self.assertIn('context.cooperate = [](void *) { vTaskDelay(1); }',crypto)
        self.assertIn('vault_gcm_aes256_decrypt',crypto)
        self.assertNotIn('psa_aead_',crypto)
        cmake=read('main/CMakeLists.txt');self.assertEqual(cmake.count('"core/vault_gcm.c"'),1)
        native=read('main/core/vault_gcm.c')
        self.assertLess(native.index('if (mismatch)'),native.index('// GCTR uses inc32(J0)'))
        self.assertIn('zeroize(output, input_len)',native)
    def test_source_check_exclusions_not_expanded(self):
        self.assertEqual(len(json.loads(read('../tooling/firmware/fixtures/superseded-contracts.json'))),45)
if __name__=='__main__':unittest.main(verbosity=2)
