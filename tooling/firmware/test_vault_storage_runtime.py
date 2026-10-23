"""Exercise the production vault loader without accessing device, keys or user data."""
from pathlib import Path
import argparse, ctypes as c, hashlib, json, os, re, struct, subprocess

ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True,type=Path)
    ap.add_argument('--run-dir',required=True,type=Path)
    a=ap.parse_args();run=a.run_dir.resolve();compiler=a.compiler.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    source=(ROOT/'main/core/vault_storage.cpp').read_text(encoding='utf-8')
    (run/'vault_storage.inc').write_text(re.sub(r'^#include .*\n','',source,flags=re.M),encoding='utf-8')
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    command=[str(compiler),'-std=c++17','-O2','-shared','-static-libgcc','-static-libstdc++',
             '-I'+str(ROOT/'main'),'-I'+str(run),str(ROOT/'../tooling/firmware/native/vault_storage_replay.cpp'),
             '-o',str(run/'vault-storage.dll')]
    build=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
    (run/'compile.log').write_bytes(build.stdout+build.stderr)
    if build.returncode:raise RuntimeError('Native compile failed: '+str(run/'compile.log'))
    class Result(c.Structure):
        _fields_=[(n,c.c_int) for n in ('ok','slot','blank')]+[(n,c.c_uint) for n in ('generation','bundle','config')]+[(n,c.c_int) for n in ('reads','writes','erases','hashes','allocations','outstanding')]+[('error',c.c_char*256),('log',c.c_char*1024)]
    Oracle=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_size_t,c.c_void_p)
    dll=c.CDLL(str(run/'vault-storage.dll'));probe=dll.probe
    probe.argtypes=[c.c_void_p,c.c_size_t,c.c_int,c.c_int,c.c_int,Oracle,c.POINTER(Result)];probe.restype=None
    checks=[]
    def slot(generation=1,b=18059,config=198,change=None):
        data=bytearray((i*37+11)&255 for i in range(b+config))
        data[:128]=bytes(128);data[:8]=b'JYVAULT2';struct.pack_into('<I',data,12,b-128)
        data[24:56]=bytes(range(1,33))
        if change:change(data)
        h=bytearray(96);h[:8]=b'JVSLOT1\0';struct.pack_into('<III',h,8,generation,b,config)
        h[32:64]=hashlib.sha256(data).digest();h[64:96]=hashlib.sha256(h[:64]).digest()
        raw=bytearray(b'\xff'*0x80000);raw[:96]=h;raw[4096:4096+len(data)]=data;return raw
    def execute(name,raw,*,ok,selected=-1,blank=False,read=0,cancel=0,allocation=0,hash_error=0,wrong=False,stage=None,selftest=None):
        calls=0
        @Oracle
        def sha(p,n,out):
            nonlocal calls
            calls+=1
            if calls==hash_error:return 0
            digest=bytearray(hashlib.sha256(c.string_at(p,n)).digest())
            if wrong:digest[0]^=1
            c.memmove(out,bytes(digest),32);return 1
        backing=c.create_string_buffer(bytes(raw));result=Result()
        probe(backing,len(raw),read,cancel,allocation,sha,c.byref(result))
        assert bool(result.ok)==ok,(name,'ok',result.error)
        assert result.slot==selected and bool(result.blank)==blank,(name,'selection')
        assert result.writes==result.erases==result.outstanding==0,(name,'side effects')
        assert result.allocations<=1,(name,'internal allocation')
        log=result.log.decode('utf-8');error=result.error.decode('utf-8')
        if stage:assert stage in log,(name,'missing stage',log)
        if selftest is not None:
            assert log.count('SHA self-test:')==2,(name,'self test count',log)
            assert log.count('matches='+str(int(selftest)))==2,(name,'self test',log)
            assert ('校验组件异常' in error)==(not selftest),(name,'self test classification',error)
        if ok:assert not error and not log,(name,'success must be quiet')
        elif not cancel:assert error,(name,'failure must be visible')
        checks.append(name)
    empty=bytes(b'\xff'*0x80000);a0,a1=slot(12),slot(13);valid=a0+a1
    execute('newest of two valid slots',valid,ok=True,selected=1)
    execute('first slot only',a0+empty,ok=True,selected=0)
    execute('second slot only',empty+a1,ok=True,selected=1)
    execute('erased partition is setup eligible',empty+empty,ok=True,blank=True)
    bad0=bytearray(a0);bad0[64]^=1
    bad1=bytearray(a1);bad1[64]^=1
    execute('invalid old slot cannot displace valid newest',bad0+a1,ok=True,selected=1)
    execute('both header mismatches are failed loads',bad0+bad1,ok=False,stage='header-digest',selftest=True)
    execute('a single non-FF byte is not an empty vault',b'\0'+(empty+empty)[1:],ok=False,stage='header-magic',selftest=True)
    bad0=bytearray(a0);bad0[4100]^=1
    bad1=bytearray(a1);bad1[4100]^=1
    execute('payload digest mismatch stays protected',bad0+bad1,ok=False,stage='payload-digest',selftest=True)
    def bad_frame(data):data[0]=0
    execute('authenticated hashes do not bypass framing',slot(change=bad_frame)+empty,ok=False,stage='bundle-framing',selftest=True)
    bad0=bytearray(a0);struct.pack_into('<I',bad0,12,0xffffffff)
    execute('untrusted length is bounded before allocation',bad0+empty,ok=False,stage='header-fields',selftest=True)
    execute('internal transfer allocation failed',valid,ok=False,allocation=1)
    execute('slot0 header I/O error forbids fallback',valid,ok=False,read=1,stage='header-read')
    execute('slot0 payload I/O error forbids fallback',valid,ok=False,read=2,stage='payload-read')
    execute('SHA call error is not data corruption',valid,ok=False,hash_error=1,stage='header-sha-operation')
    execute('silent wrong digest is detected by known-answer tests',valid,ok=False,wrong=True,stage='header-digest',selftest=False)
    execute('cancelled read is not empty or valid',valid,ok=False,cancel=1)
    execute('manual retry after failure reselects intact data',valid,ok=True,selected=1)
    # Boundary sizes straddle one-sector, block, and 1KiB SHA chunk boundaries.
    for b in (144,145,1023,1024,1025,4095,4096,4097,262144):
        execute('bundle length '+str(b),slot(b=b)+empty,ok=True,selected=0)
    inputs={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ('main/core/vault_storage.cpp','main/core/vault_storage.hpp','main/core/vault_crypto.hpp','main/core/vault_format.hpp','../tooling/firmware/native/vault_storage_replay.cpp','../tooling/firmware/test_vault_storage_runtime.py')}
    result={'passed':True,'cases':checks,'inputs':inputs,'synthetic_only':True,'device_accessed':False,
            'method':'Production storage C++, synthetic flash, independent hashlib SHA oracle and injected I/O/hash failures',
            'limitation':'Does not execute ESP32 SHA hardware, cache, PSRAM or flash driver'}
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'cases':len(checks),'device_accessed':False}))

if __name__=='__main__':main()
