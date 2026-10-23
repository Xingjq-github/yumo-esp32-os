"""Real PPU -> production scheduling/output; compare old sampling, area and 1:1."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--run-dir',type=Path,required=True)
    ap.add_argument('--baseline',type=Path,required=True)
    ap.add_argument('--area-baseline',type=Path,required=True)
    args=ap.parse_args();run=args.run_dir.resolve();baseline=args.baseline.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('gcc') or 'gcc').resolve()
    cpp=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    engine=ROOT/'firmware/components/nofrendo'
    sources=[engine/s for s in re.findall(r'"([^"\n]+\.c)"',(engine/'CMakeLists.txt').read_text()) if s!='fc_port.c']
    sources.append(ROOT/'tooling/firmware/native/fc_rendering_runtime.c')
    results={}
    def execute(name,command):
        p=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,timeout=180,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.stdout.log')).write_bytes(p.stdout);(run/(name+'.stderr.log')).write_bytes(p.stderr)
        assert p.returncode==0,name+'\n'+p.stderr.decode(errors='replace')[-6000:]
        return p.stdout.decode().strip()
    execute('ppu-compile',[cc,'-std=c11','-O2','-I'+str(engine),'-Dfree=probe_free','-o',run/'ppu.exe',*sources])
    results['ppu']=json.loads(execute('ppu-run',[run/'ppu.exe',run/'frames.bin']))
    harness=ROOT/'tooling/firmware/native/fc_rendering_schedule.cpp'
    for label,include,extra in [('before',baseline/'firmware/main',['-DEXPECT_OLD']),
                              ('area',args.area_baseline.resolve()/'firmware/main',['-DEXPECT_AREA']),
                              ('after',ROOT/'firmware/main',[])]:
        execute(label+'-compile',[cpp,'-std=c++17','-O2','-static-libgcc','-static-libstdc++','-I'+str(include),*extra,harness,'-o',run/(label+'.exe')])
        results[label]=json.loads(execute(label+'-run',[run/(label+'.exe'),run/'frames.bin']))
    paths=[*engine.rglob('*.c'),*engine.rglob('*.h'),harness,sources[-1],ROOT/'tooling/firmware/native/mmc3_variants_runtime.c',Path(__file__),ROOT/'firmware/main/core/fc_timing.hpp',ROOT/'firmware/main/core/fc_video.hpp']
    result={'passed':True,'reproduced_before_fix':True,'device_accessed':False,'commercial_rom_tested':False,'results':results,'inputs':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    result['baseline_inputs']={str(p):hashlib.sha256(p.read_bytes()).hexdigest()
        for directory in (baseline,args.area_baseline.resolve())
        for p in (directory/'firmware/main/core/fc_timing.hpp',directory/'firmware/main/core/fc_video.hpp')}
    result['benchmark_scope']='Host frame conversion only; not ESP32 FPS or full emulator throughput'
    result['host_area_to_native_ratio']=results['area']['host_scale_300_frames_us']/max(1,results['after']['host_scale_300_frames_us'])
    (run/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='inputs'}))

if __name__=='__main__':main()
