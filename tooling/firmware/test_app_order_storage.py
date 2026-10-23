"""Isolated production settings/NVS persistence and restart tests; no device data."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);run=p.parse_args().run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    code=(ROOT/'main/core/settings_store.cpp').read_text(encoding='utf-8')
    code=re.sub(r'^#include .*\n','',code,flags=re.M).replace('timeval tv','FakeTimeval tv').replace('settimeofday(','fakeSetTime(')
    (run/'settings.inc').write_text(code,encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0;\n')
    def execute(name,args):
        result=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-6000:])
        return result.stdout.decode().strip()
    exe=run/'order-storage.exe'
    execute('compile',[str(cc),'-std=c++20','-O1','-I'+str(run),'-I'+str(ROOT/'main'),str(ROOT/'../tooling/firmware/native/app_order_storage_replay.cpp'),'-o',str(exe),'-static-libgcc','-static-libstdc++'])
    outputs={mode:execute(mode,[str(exe),mode,str(run/'synthetic-order.bin')]) for mode in ('write','read','corrupt')}
    names=['main/core/settings_store.cpp','main/core/settings_store.hpp','main/core/app_order_model.hpp','../tooling/firmware/native/app_order_storage_replay.cpp','../tooling/firmware/test_app_order_storage.py']
    report=dict(passed=True,outputs=outputs,inputs={n:sha(ROOT/n) for n in names},device_accessed=False,real_storage_accessed=False)
    (run/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
if __name__=='__main__':main()
