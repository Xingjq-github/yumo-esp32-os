"""Partition-layout check retained for builds. V2 needs no authorization image."""
from pathlib import Path
import hashlib,struct
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
OFFSET,SIZE=0xEC0000,0x100000

def partition_layout(raw, *, allow_legacy=False):
    expected = [('nvs', 1, 2, 0x9000, 0x4000), ('otadata', 1, 0, 0xD000, 0x2000),
                ('phy_init', 1, 1, 0xF000, 0x1000), ('factory', 0, 0, 0x20000, 0x950000),
                ('assets', 1, 0x82, 0x970000, 0x550000),
                ('vault', 1, 0x40, OFFSET, SIZE)]
    legacy = list(expected)
    legacy[3] = ('factory', 0, 0, 0x20000, 0x550000)
    found = []
    for at in range(0, len(raw), 32):
        chunk = raw[at:at+32]
        if len(chunk) != 32:
            break
        magic = int.from_bytes(chunk[:2], 'little')
        if magic == 0xEBEB:
            if chunk[2:16] != b'\xff'*14 or chunk[16:] != hashlib.md5(raw[:at]).digest() or not (found == expected or (allow_legacy and found == legacy)):
                raise ValueError('Partition table layout/MD5 does not match this device')
            return found
        if magic != 0x50AA:
            break
        _, kind, sub, off, size, label, flags = struct.unpack('<HBBII16sI', chunk)
        if flags:
            raise ValueError('Encrypted/flagged partitions require a separate provisioning design')
        found.append((label.rstrip(b'\0').decode('ascii'), kind, sub, off, size))
    raise ValueError('Partition table missing/invalid')


def validate_partition_upgrade(current, target):
    """Only the exact previous factory layout may grow; no data may move."""
    old = partition_layout(current, allow_legacy=True)
    new = partition_layout(target)
    for before, after in zip(old, new):
        if before[0] == 'factory':
            if before[:4] != after[:4] or before[4] > after[4]:
                raise ValueError('Unsafe factory partition change')
        elif before != after:
            raise ValueError('Protected partition changed')
    return new


if __name__=='__main__':
    raise SystemExit('JYVAULT2 embeds deployment keys in firmware. Never write an authorization image or overwrite vault. Flash the current generated firmware manifest; set a numeric PIN on first open.')
