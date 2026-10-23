"""Regression checks for fresh-clone identity initialization (synthetic temporary data)."""
from pathlib import Path
import hashlib, json, tempfile, unittest
from init_config import initialize

class InitializationTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.root=Path(self.temporary.name)
        (self.root/'firmware/main/core').mkdir(parents=True)
        (self.root/'server').mkdir()
    def tearDown(self):
        self.temporary.cleanup()
    def snapshot(self):
        names=('server/deployment/local-keys.json','server/vault-sync-key.properties',
               'firmware/vault-sync-key.properties')
        return {n:hashlib.sha256((self.root/n).read_bytes()).digest() for n in names}
    def test_matching_keys_and_idempotent_reinitialization(self):
        initialize(self.root)
        self.assertTrue((self.root/'server/vault-sync-key.properties').read_bytes()==
                        (self.root/'firmware/vault-sync-key.properties').read_bytes())
        before=self.snapshot()
        initialize(self.root,url='http://example.local:8443')
        self.assertTrue(before==self.snapshot(),'Initialization must preserve identity')
        self.assertIn('http://example.local:8443',(self.root/'firmware/main/core/vault_builtin.hpp').read_text())
    def test_partial_configuration_refused_without_changes(self):
        initialize(self.root)
        (self.root/'server/vault-sync-key.properties').unlink()
        before=(self.root/'server/deployment/local-keys.json').read_bytes()
        with self.assertRaises(RuntimeError):initialize(self.root)
        self.assertTrue(before==(self.root/'server/deployment/local-keys.json').read_bytes())
    def test_different_sync_keys_refused(self):
        initialize(self.root)
        (self.root/'server/vault-sync-key.properties').write_text('sync.key.hex='+'ab'*32+'\n')
        with self.assertRaises(ValueError):initialize(self.root)
    def test_sync_rotation_preserves_storage_identity(self):
        initialize(self.root)
        before=(self.root/'server/deployment/local-keys.json').read_bytes()
        for name in ('server','firmware'):
            (self.root/name/'vault-sync-key.properties').write_text('sync.key.hex='+'cd'*32+'\n')
        initialize(self.root)
        self.assertTrue(before==(self.root/'server/deployment/local-keys.json').read_bytes())
    def test_new_checkouts_get_different_identities(self):
        initialize(self.root)
        before=self.snapshot()
        other=self.root/'other'
        (other/'firmware/main/core').mkdir(parents=True)
        (other/'server').mkdir()
        initialize(other)
        self.assertTrue(all(hashlib.sha256((other/n).read_bytes()).digest()!=v for n,v in before.items()))
    def test_invalid_urls_do_not_create_configuration(self):
        for url in ('file:///tmp','http://user:pass@example.local','http://example.local/path',
                    'http://example.local/?secret=1','http://example.local:99999'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                initialize(self.root,url)
        self.assertFalse((self.root/'server/deployment/local-keys.json').exists())

if __name__=='__main__':unittest.main()
