"""Replay production mouse enqueue/host dispatch against a bounded fake radio."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT=Path(__file__).resolve().parents[2]

def section(text,start,end):
    begin=text.index(start)
    return text[begin:text.index(end,begin+len(start))]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir',required=True,type=Path)
    args=ap.parse_args();run=args.run_dir.resolve()
    if not run.is_relative_to((ROOT/'artifacts/validation').resolve()):
        raise ValueError('run-dir must stay under artifacts/validation')
    run.mkdir(parents=True,exist_ok=False)
    source=ROOT/'firmware/main/core/ble_hid.cpp'
    text=source.read_text(encoding='utf-8')
    (run/'mouse_scheduler.inc').write_text(section(text,'// Mouse producers do no GATT work.',
                                                   'std::atomic<bool> s_presenceScan'),encoding='utf-8')
    (run/'mouse_api.inc').write_text(section(text,'uintptr_t BleHid::mouseLinkGeneration()',
                                             'esp_err_t BleHid::sendConsumer('),encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n',encoding='utf-8')
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve()
    harness=ROOT/'tooling/firmware/native/ble_mouse_replay.cpp'
    exe=run/'mouse-replay.exe'
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def execute(name,args):
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=120,
                         creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:
            raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-7000:])
        return p.stdout.decode().strip()
    execute('compile',[str(cc),'-std=c++17','-O2','-g','-Wall','-Wextra','-Werror',
                       '-static-libgcc','-static-libstdc++','-I'+str(run),
                       '-I'+str(ROOT/'firmware/main'),str(harness),'-o',str(exe)])
    output=execute('replay',[str(exe)])
    result=json.loads(output)
    paths=[source,ROOT/'firmware/main/core/ble_hid.hpp',ROOT/'firmware/main/core/hid_mouse_queue.hpp',
           Path(__file__).resolve(),harness]
    result.update(inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                  production_source_extracted=True,device_accessed=False,
                  limits='Real queue, enqueue and host dispatch functions; synthetic time/radio, not RF throughput or host cursor measurement.')
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(output)

if __name__=='__main__':main()
