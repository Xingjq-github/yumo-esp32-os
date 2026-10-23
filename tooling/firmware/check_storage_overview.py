"""Data-preserving partition expansion; no build artifact or device required."""
import hashlib
from pathlib import Path
import struct
import unittest
from prepare_vault_flash import partition_layout, validate_partition_upgrade

ROOT=Path(__file__).resolve().parents[2]
ROWS=[('nvs',1,2,0x9000,0x4000),('otadata',1,0,0xd000,0x2000),
      ('phy_init',1,1,0xf000,0x1000),('factory',0,0,0x20000,0x950000),
      ('assets',1,0x82,0x970000,0x550000),('vault',1,0x40,0xec0000,0x100000)]
def table(rows):
    raw=b''.join(struct.pack('<HBBII16sI',0x50aa,kind,sub,offset,size,label.encode(),0)
                 for label,kind,sub,offset,size in rows)
    return raw+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(raw).digest()+b'\xff'*32

class StorageOverview(unittest.TestCase):
    def test_expands_only_unused_gap(self):
        old=list(ROWS);old[3]=(*old[3][:4],0x550000)
        self.assertEqual(validate_partition_upgrade(table(old),table(ROWS)),ROWS)
        self.assertEqual(validate_partition_upgrade(table(ROWS),table(ROWS)),ROWS)
        self.assertEqual(ROWS[3][4]-old[3][4],4*1024*1024)
        self.assertEqual(ROWS[3][3]+ROWS[3][4],ROWS[4][3])
    def test_legacy_is_not_a_valid_build_or_postflash_target(self):
        old=list(ROWS);old[3]=(*old[3][:4],0x550000)
        with self.assertRaises(ValueError):partition_layout(table(old))
        with self.assertRaises(ValueError):validate_partition_upgrade(table(ROWS),table(old))
    def test_rejects_any_other_layout_or_corruption(self):
        for i in range(len(ROWS)):
            for field in (3,4):
                bad=list(ROWS);entry=list(bad[i]);entry[field]+=0x1000;bad[i]=tuple(entry)
                with self.assertRaises(ValueError):validate_partition_upgrade(table(bad),table(ROWS))
        raw=bytearray(table(ROWS));raw[8]^=1
        with self.assertRaises(ValueError):partition_layout(raw)
    def test_csv_matches_new_layout(self):
        rows=[]
        for line in (ROOT/'firmware/partitions.csv').read_text().splitlines():
            if not line.strip() or line.lstrip().startswith('#'):continue
            f=[v.strip() for v in line.split(',')]
            rows.append((f[0],int(f[3],0),int(f[4],0)))
        self.assertEqual(rows,[(r[0],r[3],r[4]) for r in ROWS])

if __name__=='__main__':unittest.main(verbosity=2)
