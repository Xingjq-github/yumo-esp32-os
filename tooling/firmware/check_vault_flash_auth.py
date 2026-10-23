"""Current embedded-key deployment contracts; supersedes legacy authorization-image tests."""
import json,re,unittest
from pathlib import Path
from prepare_vault_flash import partition_layout
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
class EmbeddedKeys(unittest.TestCase):
    def test_keys_match_without_printing_secrets(self):
        keys=json.loads((ROOT/'../server/deployment/local-keys.json').read_text(encoding='utf-8-sig'))
        header=(ROOT/'main/core/vault_builtin.hpp').read_text()
        for name,field in [('Key','key'),('Id','vault_id'),('Salt','salt')]:
            raw=re.search(r'\b'+name+r'\[\] = \{([^}]+)\}',header).group(1)
            self.assertTrue(bytes(int(v,16) for v in raw.split(','))==bytes.fromhex(keys[field]))
        self.assertTrue(keys['token'] in header)
    def test_no_extra_region(self):
        args=json.loads((ROOT/'../artifacts/firmware/flasher_args.json').read_text())
        self.assertEqual({int(p,0) for p in args['flash_files']},{0,0x8000,0xd000,0x20000})
        self.assertEqual(partition_layout((ROOT/'../artifacts/firmware/partition_table/partition-table.bin').read_bytes())[-1],('vault',1,0x40,0xEC0000,0x100000))
    def test_nas_mounts_stable_identity_read_only(self):
        compose=(ROOT/'../packaging/nas/compose.yaml').read_text(encoding='utf-8')
        self.assertIn('source: ./deployment',compose)
        self.assertIn('target: /app/deployment',compose)
        self.assertIn('VAULT_KEYS_FILE: /app/deployment/local-keys.json',compose)
        self.assertIn('read_only: true',compose)
        code=(ROOT/'../tooling/init_config.py').read_text(encoding='utf-8-sig')
        self.assertIn('if any(have) and not all(have)',code)
        self.assertIn('if all(have)',code)
if __name__=='__main__':unittest.main(verbosity=2)
