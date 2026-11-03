"""Replay production PBKDF2 against hashlib, with measured SHA work and fault injection.

Uses synthetic credentials only; no user vault, device access or firmware build.
The local SHA source is instrumented only to count compression calls and inject
failures/cancellation at its public boundaries. Output wiping is observed after
calling the production volatile wipe implementation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import random
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "core/vault_crypto.hpp"
#include "core/vault_sha256.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
static unsigned checks=0,compressions=0,sha_calls=0,yields=0,wipes=0,owners_wiped=0;
static unsigned trigger=0,mode=0,allocations=0;
static bool screen_on=true,sleep_pending=false;
static std::atomic<bool> cancelled{false};
static void check(bool ok,int line){++checks;if(!ok){fprintf(stderr,"check line %d\n",line);exit(1);}}
#define CHECK(v) check((v),__LINE__)
void *operator new(size_t n){++allocations;if(void *p=malloc(n))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{free(p);}
void operator delete(void *p,size_t)noexcept{free(p);}
void vTaskDelay(int ticks){CHECK(ticks==1);++yields;}
namespace sys {struct Power {
    static bool isScreenOn(){return screen_on;}
    static bool sleepPending(){return sleep_pending;}
};}
static bool sha_boundary(){
    ++sha_calls;
    if(sha_calls!=trigger)return false;
    if(mode==1)return true;
    if(mode==2)cancelled=true;
    if(mode==3)screen_on=false;
    if(mode==4)sleep_pending=true;
    return false;
}
// The C primitive is unchanged except this compression counter and renamed
// public functions, allowing faults to be injected outside the primitive.
#define vault_sha256_process raw_sha256_process
#define vault_sha256_done raw_sha256_done
#include "sha_counted.inc"
#undef vault_sha256_process
#undef vault_sha256_done
extern "C" int vault_sha256_process(vault_sha256_state *s,const unsigned char *p,unsigned long n){
    return sha_boundary()?-1:raw_sha256_process(s,p,n);
}
extern "C" int vault_sha256_done(vault_sha256_state *s,unsigned char *p){
    return sha_boundary()?-1:raw_sha256_done(s,p);
}
namespace vault {
static void observed_wipe(void *p,size_t n){
    vault::wipe(p,n);++wipes;
    auto bytes=static_cast<const unsigned char *>(p);
    for(size_t i=0;i<n;++i)CHECK(bytes[i]==0);
    if(n>200)++owners_wiped;
}
}
#define wipe observed_wipe
#include "derive_production.inc"
#undef wipe
static void reset(unsigned next=0,unsigned type=0){
    compressions=sha_calls=yields=wipes=owners_wiped=0;
    trigger=next;mode=type;cancelled=false;screen_on=true;sleep_pending=false;
}
static void unhex(const char *s,unsigned char *p,size_t n){
    for(size_t i=0;i<n;++i){unsigned v=0;CHECK(sscanf(s+2*i,"%2x",&v)==1);p[i]=v;}
}
static bool zero(const vault::Key &k){return std::all_of(k.bytes.begin(),k.bytes.end(),[](uint8_t b){return b==0;});}
struct Vector {const char *password,*salt,*expected;};
static const Vector vectors[]={@VECTORS@};
int main(){
    unsigned vectors_tested=0,faults=0;
    for(const auto &v:vectors){
        unsigned char salt[16],expected[32];unhex(v.salt,salt,16);unhex(v.expected,expected,32);
        vault::Key out;out.bytes.fill(0xa5);reset();const auto old_allocations=allocations;
        CHECK(vault::derive(v.password,strlen(v.password),salt,out,cancelled));
        CHECK(memcmp(out.bytes.data(),expected,32)==0);CHECK(allocations==old_allocations);
        CHECK(compressions==20002 && sha_calls==40002 && yields==9 && owners_wiped==1);++vectors_tested;
    }
    unsigned char salt[16]{};vault::Key out;
    for(const char *bad:{"","0","012","0123456789012","abcd","012x","123\xff"}){
        reset();out.bytes.fill(0xa5);CHECK(!vault::derive(bad,strlen(bad),salt,out,cancelled));
        CHECK(zero(out)&&compressions==0&&owners_wiped==0);++faults;
    }
    reset();out.bytes.fill(0xa5);CHECK(!vault::derive(nullptr,4,salt,out,cancelled));CHECK(zero(out));++faults;
    reset();out.bytes.fill(0xa5);CHECK(!vault::derive("0125",4,nullptr,out,cancelled));CHECK(zero(out));++faults;
    for(unsigned type=2;type<=4;++type){
        reset();if(type==2)cancelled=true;if(type==3)screen_on=false;if(type==4)sleep_pending=true;
        out.bytes.fill(0xa5);CHECK(!vault::derive("0125",4,salt,out,cancelled));
        CHECK(zero(out)&&compressions==0&&owners_wiped==0);++faults;
    }
    // Init, both halves of U1, mid-stream and final U10000 boundaries.
    const unsigned points[]={1,2,3,4,5,6,7,11,100,1025,1026,40000,40001,40002};
    for(unsigned type=1;type<=4;++type)for(unsigned point:points){
        reset(point,type);out.bytes.fill(0xa5);
        CHECK(!vault::derive("0125",4,salt,out,cancelled));
        CHECK(zero(out)&&owners_wiped==1&&sha_calls>=point);
        // The two prefix setup calls precede the loop's 256-HMAC checkpoints.
        CHECK(type==1?sha_calls==point:sha_calls-std::max(point,2u)<=1024);++faults;
    }
    // Every new request recomputes both keyed prefixes, including after faults.
    reset();CHECK(vault::derive("0125",4,salt,out,cancelled));auto first=out.bytes;
    CHECK(vault::derive("0126",4,salt,out,cancelled));CHECK(first!=out.bytes);
    CHECK(compressions==40004&&owners_wiped==2&&yields==18);
    printf("{\"passed\":true,\"checks\":%u,\"hashlib_vectors\":%u,\"fault_cases\":%u,\"rounds\":10000,\"compression_calls_per_derive\":20002,\"yield_calls_per_derive\":9,\"new_allocations_per_derive\":0,\"keyed_owner_size\":%u}\n",checks,vectors_tested,faults,unsigned(sizeof(vault::HmacSha256)));
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run-dir', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True, help='External host g++ compiler path')
    a = p.parse_args()
    run = a.run_dir.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    main_dir = ROOT/'firmware/main'
    crypto = (main_dir/'core/vault_crypto.cpp').read_text(encoding='utf8')
    # Production owner/hash/derive text; unrelated AES/GCM functions not linked.
    body = crypto[crypto.index('namespace vault {'):crypto.index('bool gcm(')]+'\n}\n'
    (run/'derive_production.inc').write_text(body,encoding='utf8')
    sha = (main_dir/'core/vault_sha256.c').read_text(encoding='utf8')
    sha = re.sub(r'^#include "vault_sha256.h"\n','',sha,flags=re.M)
    marker = 'static int vault_sha256_compress(struct vault_sha256_state *md, unsigned char *buf)\n{'
    assert sha.count(marker) == 1
    sha = sha.replace(marker,marker+'\n    ++compressions;')
    (run/'sha_counted.inc').write_text(sha,encoding='utf8')
    rng = random.Random(20261030)
    vectors = [(pw,salt) for pw in ('0000','0125','123456789','012345678901')
               for salt in (bytes(16),bytes([255])*16,bytes(range(16)))]
    vectors += [(''.join(str(rng.randrange(10)) for _ in range(length)),rng.randbytes(16))
                for length in range(4,13) for _ in range(8)]
    rows=[]
    for pw,salt in vectors:
        expected = hashlib.pbkdf2_hmac('sha256',pw.encode(),salt,10000,32).hex()
        rows.append('{'+','.join(json.dumps(v) for v in (pw,salt.hex(),expected))+'}')
    (run/'kdf.cpp').write_text(HARNESS.replace('@VECTORS@',',\n'.join(rows)),encoding='utf8')
    compiler = a.compiler.resolve()
    env = dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    exe = run/'kdf.exe'
    cmd = [str(compiler),'-std=c++17','-O2','-I'+str(main_dir),str(run/'kdf.cpp'),'-o',str(exe)]
    for stage,command in [('compile',cmd),('execute',[str(exe)])]:
        result = subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=90)
        (run/(stage+'.stdout.log')).write_bytes(result.stdout)
        (run/(stage+'.stderr.log')).write_bytes(result.stderr)
        if result.returncode:
            print(result.stderr.decode(errors='replace')[-10000:]);raise RuntimeError(stage+' failed')
    output = json.loads(result.stdout.decode().strip().splitlines()[-1])
    hashes = {str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
              for f in (main_dir/'core/vault_crypto.cpp',main_dir/'core/vault_crypto.hpp',main_dir/'core/vault_sha256.c',main_dir/'core/vault_sha256.h',main_dir/'core/vault_format.hpp',Path(__file__))}
    report = dict(passed=True,replay=output,sha256=hashes,
        method='Byte-identical production HMAC owner/hash/derive; existing C SHA primitive with compression counter; hashlib.pbkdf2_hmac oracle; public SHA calls instrumented for failure/cancel/screen/sleep; production volatile wipe observed in-place.',
        limits='Host correctness/work-count model, not ESP32 timing. IDF scheduling/power stubbed; no firmware build/device/user-data access.')
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
