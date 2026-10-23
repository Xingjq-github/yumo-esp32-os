"""Run production worker-wake and idle paths with synthetic RTOS/audio only."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    inputs=[]
    for name,path in [('wake','main/core/worker_wake.cpp'),('game_audio','main/core/game_audio.cpp')]:
        source=ROOT/path;inputs.append(source)
        (run/(name+'.inc')).write_text(re.sub(r'^#include .*\n','',source.read_text(encoding='utf-8'),flags=re.M),encoding='utf-8')
    source=ROOT/'main/core/fc_emulator.cpp';inputs.append(source)
    body=source.read_text(encoding='utf-8');body=body.split('    if (job.paused.load()) {',1)[1].split('\n    }\n    sys::Power::WorkGuard work;',1)[0]
    (run/'fc_pause.inc').write_text(body,encoding='utf-8')
    def execute(name,args):
        r=subprocess.run([str(x) for x in args],cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace')[-7000:])
        return r.stdout.decode().strip()
    test=ROOT/'../tooling/firmware/native/worker_wait_replay.cpp';inputs.append(test)
    execute('compile',[cc,'-std=c++17','-O1','-Wall','-Wextra','-I'+str(ROOT/'main'),'-I'+str(run),test,'-o','worker-wait.exe','-static-libgcc','-static-libstdc++'])
    output=execute('run',[run/'worker-wait.exe'])
    inputs += [ROOT/('main/core/'+name) for name in ['worker_wake.hpp','game_audio.hpp','fc_emulator.hpp','fc_autosave.hpp']]
    result=dict(passed=True,output=output,synthetic_only=True,device_accessed=False,inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
