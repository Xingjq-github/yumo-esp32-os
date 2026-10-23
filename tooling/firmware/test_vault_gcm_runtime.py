"""Execute production C AES-GCM against independent cryptography and Java fixtures.
Uses synthetic data only; never reads pc/data, firmware keys or a device.
"""
from pathlib import Path
import argparse, ctypes as c, hashlib, json, random, struct, subprocess, time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--run-dir',type=Path,required=True);args=p.parse_args()
    run=args.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=True)
    dll=run/'vault_gcm.dll'
    build=subprocess.run([str(args.compiler),'-shared','-Imain/core','-o',str(dll),
        '../tooling/firmware/native/vault_gcm_probe.c','main/core/vault_gcm.c'],cwd=ROOT,capture_output=True)
    (run/'compile.stdout.log').write_bytes(build.stdout);(run/'compile.stderr.log').write_bytes(build.stderr)
    if build.returncode:raise RuntimeError('Native compile failed; see compile.stderr.log')
    native=c.CDLL(str(dll));probe=native.probe
    probe.argtypes=[c.c_int,c.c_void_p,c.c_void_p,c.c_void_p,c.c_uint,c.c_void_p,c.c_uint,c.c_void_p,c.c_void_p,c.POINTER(c.c_uint)]
    probe.restype=c.c_int
    count=0;yields=0
    def call(enc,key,nonce,aad,data,tag=b'\0'*16,alias=False,expected=0):
        nonlocal count,yields
        # Guard output on BOTH sides. All sizes include empty/partial AES blocks.
        raw=c.create_string_buffer(len(data)+32);raw.raw=b'\xa5'*(len(data)+32)
        output=c.byref(raw,16)
        if alias:
            c.memmove(output,data,len(data));source=output
        else:source=c.create_string_buffer(data)
        t=c.create_string_buffer(tag,16);n=c.c_uint()
        rc=probe(enc,key,nonce,aad,len(aad),source,len(data),output,t,c.byref(n))
        assert rc==expected,(enc,len(data),rc,expected)
        assert raw.raw[:16]==b'\xa5'*16 and raw.raw[16+len(data):]==b'\xa5'*16
        count+=1;yields+=n.value
        return raw.raw[16:16+len(data)],t.raw
    rng=random.Random(20260923)
    def data(n):return bytes(rng.randrange(256) for _ in range(n))
    start=time.monotonic()
    for size in [0,1,15,16,17,31,32,33,1023,1024,1999,2000,2001,2047,2048,2049,4096,8191,16384,16472,262000]:
        for alen in [0,1,16,68,128]:
            k,n,a,plain=data(32),data(12),data(alen),data(size)
            oracle=AESGCM(k).encrypt(n,plain,a)
            out,tag=call(1,k,n,a,plain)
            assert out+tag==oracle
            for alias in (False,True):
                back,_=call(0,k,n,a,out,tag,alias=alias);assert back==plain
            bad=bytearray(tag);bad[-1]^=1
            rejected,_=call(0,k,n,a,out,bytes(bad),expected=1)
            assert rejected==b'\0'*size
    for _ in range(80):
        plain,k,n,a=data(rng.randrange(8192)),data(32),data(12),data(rng.randrange(145))
        oracle=AESGCM(k).encrypt(n,plain,a)
        out,tag=call(1,k,n,a,plain,alias=True);assert out+tag==oracle
        back,_=call(0,k,n,a,out,tag);assert back==plain
        for which in ('key','nonce','aad','cipher'):
            vals=[bytearray(k),bytearray(n),bytearray(a),bytearray(out)]
            index=('key','nonce','aad','cipher').index(which)
            if not vals[index]:continue
            vals[index][rng.randrange(len(vals[index]))]^=1
            rejected,_=call(0,bytes(vals[0]),bytes(vals[1]),bytes(vals[2]),bytes(vals[3]),tag,expected=1)
            assert rejected==b'\0'*len(out)
    # Existing protocol fixtures were generated independently; synthetic credentials.
    fixture=json.loads((ROOT/'../server/src/test/resources/compatibility.json').read_text())
    import base64
    key=bytes.fromhex(fixture['keys']['key'])
    for vector in fixture['vectors']:
        bundle=base64.b64decode(vector['blob'])
        dk,_=call(0,key,bundle[56:68],bundle[:68],bundle[68:100],bundle[100:116])
        plain,_=call(0,dk,bundle[116:128],bundle[:128],bundle[128:-16],bundle[-16:])
        assert json.loads(plain)['entries']==vector['entries']
    # Local PIN config continues decrypting with stable storage key across sync-key rotation.
    stable=data(32);identity=data(32)
    config=json.dumps({'schema':2,'url':'http://nas.test:8443','pin_salt':'1'*32,'pin_hash':'2'*64}).encode()
    nonce=data(12);aad=b'JYCONFIG2'+identity
    sealed=AESGCM(stable).encrypt(nonce,config,aad)
    proofs=set()
    for sync_key in (data(32),data(32),data(32)):
        proofs.add(hashlib.sha256(b'JYVAULT-DEPLOY-KEY-V2'+sync_key).digest())
        plain,_=call(0,stable,nonce,aad,sealed[:-16],sealed[-16:]);assert plain==config
    assert len(proofs)==3 and yields>0
    report={'passed':True,'native_calls':count,'cooperative_yields':yields,
            'seconds':round(time.monotonic()-start,2),'synthetic_only':True,
            'driver':'production main/core/vault_gcm.c via TinyCC DLL',
            'inputs':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in
                      ('main/core/vault_gcm.c','main/core/vault_gcm.h','../tooling/firmware/native/vault_gcm_probe.c')}}
    (run/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))
if __name__=='__main__':main()
