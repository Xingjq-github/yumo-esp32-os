"""Production save service with deterministic SD/RTOS/checkpoint adapters."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    src=ROOT/'main/core/fc_saves.cpp'
    (run/'service.inc').write_text(re.sub(r'^#include .*\n','',src.read_text(encoding='utf-8'),flags=re.M),encoding='utf-8')
    def execute(name,cmd):
        p=subprocess.run([str(x) for x in cmd],cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-6000:])
        return p.stdout.decode().strip()
    test=ROOT/'../tooling/firmware/native/fc_autosave_replay.cpp'
    execute('compile',[cc,'-std=c++20','-O1','-Wall','-Wextra','-DESTALE=116','-I'+str(ROOT/'main'),'-I'+str(run),test,'-o','autosave.exe','-static-libgcc','-static-libstdc++'])
    output=execute('run',[run/'autosave.exe'])
    files=[src,test,Path(__file__).resolve()]+[ROOT/('main/core/'+p) for p in ['fc_saves.hpp','fc_save_store.h','fc_emulator.hpp','fc_autosave.hpp','sd_media.hpp','worker_wake.hpp']]
    report=dict(passed=True,output=output,synthetic_only=True,device_accessed=False,inputs={Path(os.path.relpath(p,ROOT)).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
