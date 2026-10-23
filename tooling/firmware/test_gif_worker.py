"""Replay actual GIF worker/frame-clock code with synthetic RTOS/SD/decoder."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    source=ROOT/'main/core/gif_player.cpp'
    (run/'gif_player.inc').write_text(re.sub(r'^#include .*\n','',source.read_text(encoding='utf-8'),flags=re.M),encoding='utf-8')
    args=[str(cc),'-std=c++17','-O2','-Wall','-Wextra','-static-libgcc','-static-libstdc++',
        '-I'+str(run),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'components/photo_gif'),
        str(ROOT/'../tooling/firmware/native/gif_worker_replay.cpp'),'-o',str(run/'gif-worker.exe')]
    for name,cmd in [('compile',args),('run',[str(run/'gif-worker.exe')])]:
        p=subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
    result=json.loads(p.stdout)
    result.update(method='Production worker and Job frame clock, fake RTOS/SD/decoder with controlled costs and GUI timing',
        device_accessed=False,inputs={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
        for f in [source,ROOT/'main/core/gif_player.hpp',ROOT/'../tooling/firmware/native/gif_worker_replay.cpp']})
    (run/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))

if __name__=='__main__':main()
