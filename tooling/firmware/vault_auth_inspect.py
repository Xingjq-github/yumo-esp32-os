"""Read-only auth metadata inspection; never returns credentials or config plaintext."""
from pathlib import Path
import hashlib, json, re, struct
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

def summarize(raw, key):
    if len(raw) != 0x100000: raise ValueError('Unexpected vault partition size')
    slots=[]
    for index in (0,1):
        offset=index*0x80000;header=raw[offset:offset+96]
        if header[:8]!=b'JVSLOT1\0':continue
        generation,bundle_size,config_size=struct.unpack_from('<III',header,8)
        if not generation or not 144<=bundle_size<=262144 or not 28<=config_size<=8192 or any(header[20:32]):continue
        if hashlib.sha256(header[:64]).digest()!=header[64:96]:continue
        data=raw[offset+4096:offset+4096+bundle_size+config_size]
        if hashlib.sha256(data).digest()!=header[32:64]:continue
        bundle=data[:bundle_size]
        if bundle[:8]!=b'JYVAULT2' or struct.unpack_from('<I',bundle,12)[0]!=bundle_size-128:continue
        slots.append((index,generation,bundle,data[bundle_size:]))
    if not slots:
        if all(value==255 for value in raw):return {'credential':'unconfigured','needs_pattern_setup':True,'config_authenticated':False}
        raise ValueError('No valid vault slot; initialization is not authorized')
    active=max(slots,key=lambda item:item[1]) # match vault_storage.cpp, slot 0 wins ties
    index,generation,bundle,config=active
    plain=bytearray(AESGCM(bytes(key)).decrypt(config[:12],config[12:],b'JYCONFIG2'+bundle[24:56]))
    try:
        value=json.loads(plain)
        if value.get('schema')!=2:raise ValueError('Unsupported config schema')
        if not re.fullmatch('[0-9a-f]{32}',value.get('pin_salt','')) or not re.fullmatch('[0-9a-f]{64}',value.get('pin_hash','')):raise ValueError('Invalid verifier metadata')
        if 'auth' in value and value['auth']!='pattern-v1':raise ValueError('Unsupported credential kind')
        kind='pattern-v1' if 'auth' in value else 'legacy-pin'
        return {'credential':kind,'needs_pattern_setup':kind=='legacy-pin','config_authenticated':True,
                'slot':index,'generation':generation,'encrypted_bundle_sha256':hashlib.sha256(bundle).hexdigest()}
    finally:
        plain[:]=bytes(len(plain))

def inspect_device_bytes(raw, root):
    # Read the same fixed storage key used by this build; never print or save it.
    source=(Path(root)/'firmware/main/core/vault_builtin.hpp').read_text(encoding='utf-8')
    match=re.search(r'constexpr\s+uint8_t\s+Key\[\]\s*=\s*\{([^}]+)\}',source)
    if not match:raise ValueError('Storage key declaration not found')
    key=bytearray(int(part.strip(),0) for part in match[1].split(',') if part.strip())
    if len(key)!=32:raise ValueError('Storage key length invalid')
    try:return summarize(raw,key)
    finally:key[:]=bytes(len(key))
