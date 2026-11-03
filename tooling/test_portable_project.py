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

    def test_https_urls_preserve_existing_identity_and_normalize_root_slash(self):
        initialize(self.root)
        before=self.snapshot()
        for url,expected in (
            ('https://vault.example.com', 'https://vault.example.com'),
            ('https://vault.example.com/', 'https://vault.example.com'),
            ('https://vault.example.com:443/', 'https://vault.example.com:443'),
            ('https://vault.example.com:9443', 'https://vault.example.com:9443'),
            ('http://192.0.2.10:9080/', 'http://192.0.2.10:9080'),
        ):
            with self.subTest(url=url):
                initialize(self.root,url)
                self.assertEqual(before,self.snapshot(),'URL changes must never replace keys')
                saved=json.loads((self.root/'firmware/vault-connection.json').read_text())
                self.assertEqual(saved['url'],expected)
                self.assertIn(json.dumps(expected),(self.root/'firmware/main/core/vault_builtin.hpp').read_text())
                initialize(self.root)
                self.assertEqual(before,self.snapshot())
                self.assertEqual(json.loads((self.root/'firmware/vault-connection.json').read_text())['url'],expected)
    def test_invalid_https_urls_leave_all_existing_files_unchanged(self):
        initialize(self.root,url='https://vault.example.com')
        before={p.relative_to(self.root):p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        for url in ('https://', 'https://user:pass@vault.example.com',
                    'https://vault.example.com/path', 'https://vault.example.com/?secret=1',
                    'https://vault.example.com/#fragment', 'https://vault.example.com:0',
                    'https://vault.example.com:65536', 'https://vault.example.com:bad'):
            with self.subTest(url=url),self.assertRaises(ValueError):
                initialize(self.root,url)
            self.assertEqual(before,{p.relative_to(self.root):p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

if __name__=='__main__':unittest.main()
