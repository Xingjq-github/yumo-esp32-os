"""Actual bounded decoder, callback streaming/precise seek and LRC parser. Fixtures only."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--vectors',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    (run/'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void *heap_caps_malloc(size_t n,unsigned c){(void)c;return malloc(n);}
static inline void *heap_caps_realloc(void*p,size_t n,unsigned c){(void)c;return realloc(p,n);}
static inline void heap_caps_free(void*p){free(p);}
''')
    def call(name,args):
        p=subprocess.run([str(x) for x in args],env=env,cwd=run,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-6000:])
        return p.stdout
    flags=['-O2','-DMINIMP3_NO_STDIO','-DMINIMP3_ONLY_MP3','-DMINIMP3_NO_SIMD','-I'+str(run),'-I'+str(ROOT/'firmware/main'),'-I'+str(ROOT/'firmware/components/mp3_decoder/include')]
    call('decoder',[Path(__import__('shutil').which('gcc') or 'gcc').resolve(),*flags,'-c',ROOT/'firmware/components/mp3_decoder/decoder.c','-o','decoder.o'])
    call('compile',[cc,*flags,'-std=c++17',ROOT/'tooling/firmware/native/mp3_decoder_test.cpp','decoder.o','-static-libgcc','-static-libstdc++','-o','decoder.exe'])
    names=['l3-compl.bit','l3-he_32khz.bit','l3-he_44khz.bit','l3-he_48khz.bit','l3-he_free.bit','l3-nonstandard-id3v1.bit','l3-nonstandard-id3v2.bit','l3-nonstandard-apetag.bit','M2L3_bitrate_16_all.bit','M2L3_bitrate_22_all.bit','M2L3_bitrate_24_all.bit','l3-nonstandard-sin1k0db_lame_vbrtag.bit']
    names+=['l3-he_mode.bit','l3-nonstandard-he_44_48khz.bit']
    paths=[a.vectors.resolve()/n for n in names];assert all(p.is_file() for p in paths)
    result=json.loads(call('run',[run/'decoder.exe',*paths]));result['fixtures']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    result['physical_audio_tested']=False;(run/'result.json').write_text(json.dumps(result,indent=2));print(result)
if __name__=='__main__':main()
