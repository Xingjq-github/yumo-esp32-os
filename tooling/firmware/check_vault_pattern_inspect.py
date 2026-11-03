"""Synthetic read-only flash inspection, using the actual delivery metadata parser."""
import hashlib,json,struct,unittest
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from vault_auth_inspect import summarize

KEY=bytes(range(32))
def slot(generation=1,auth=None,bad_tag=False):
    bundle=bytearray(144);bundle[:8]=b'JYVAULT2';struct.pack_into('<I',bundle,12,16)
    bundle[24:56]=bytes(range(32))
    value={'schema':2,'url':'http://example.test:8443','pin_salt':'a'*32,'pin_hash':'b'*64}
    if auth is not None:value['auth']=auth
    nonce=bytes(12);cipher=bytearray(AESGCM(KEY).encrypt(nonce,json.dumps(value).encode(),b'JYCONFIG2'+bundle[24:56]))
    if bad_tag:cipher[-1]^=1
    config=nonce+cipher;data=bundle+config
    header=bytearray(96);header[:8]=b'JVSLOT1\0';struct.pack_into('<III',header,8,generation,len(bundle),len(config))
    header[32:64]=hashlib.sha256(data).digest();header[64:96]=hashlib.sha256(header[:64]).digest()
    raw=bytearray(b'\xff'*0x80000);raw[:96]=header;raw[4096:4096+len(data)]=data;return raw

class PatternInspectionContracts(unittest.TestCase):
    def test_legacy_requests_setup_without_exposing_verifier(self):
        result=summarize(slot()+b'\xff'*0x80000,KEY)
        self.assertTrue(result['config_authenticated']);self.assertTrue(result['needs_pattern_setup'])
        self.assertEqual(result['credential'],'legacy-pin')
        for field in ('pin_salt','pin_hash','url','key','password'):self.assertNotIn(field,result)
    def test_newest_pattern_does_not_request_setup_again(self):
        result=summarize(slot(8)+slot(9,'pattern-v1'),KEY)
        self.assertEqual(result['slot'],1);self.assertFalse(result['needs_pattern_setup'])
        self.assertEqual(result['credential'],'pattern-v1')
    def test_authenticated_unknown_kind_and_tag_failure_do_not_fall_back(self):
        for new in (slot(9,'future'),slot(9,'pattern-v1',True)):
            with self.assertRaises(Exception):summarize(slot(8)+new,KEY)
    def test_blank_is_distinct_from_damaged_storage(self):
        self.assertEqual(summarize(b'\xff'*0x100000,KEY)['credential'],'unconfigured')
        with self.assertRaises(ValueError):summarize(bytes(0x100000),KEY)
        with self.assertRaises(ValueError):summarize(b'\xff',KEY)

if __name__=='__main__':unittest.main(verbosity=2)
