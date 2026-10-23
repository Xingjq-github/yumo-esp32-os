"""Run production speed statistics and history storage against real host files."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--run-dir',type=Path,required=True)
    args=parser.parse_args();compiler=args.compiler.resolve();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    cjson=ROOT/'dependencies/managed_components/espressif__cjson/cJSON'
    includes=['-DESTALE=116','-I'+str(ROOT/'firmware/main'),'-I'+str(cjson)]
    commands=[
        ('model-compile',[str(compiler),'-std=c++17','-O1',*includes,str(ROOT/'tooling/firmware/speed_test_model_test.cpp'),'-o',str(run/'model.exe')]),
        ('model-run',[str(run/'model.exe')]),
        ('cjson-compile',[str(compiler.with_name('gcc.exe')),'-O1','-c',str(cjson/'cJSON.c'),'-o',str(run/'cjson.o')]),
        ('store-compile',[str(compiler),'-std=c++17','-O1',*includes,str(ROOT/'tooling/firmware/speed_test_store_host.cpp'),str(ROOT/'firmware/main/core/speed_test_store.cpp'),str(run/'cjson.o'),'-o',str(run/'store.exe')]),
        ('store-run',[str(run/'store.exe'),str(run/'card')]),
    ]
    stages=[]
    for name,command in commands:
        p=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=120)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        stages.append({'stage':name,'exit_code':p.returncode})
        if p.returncode:
            print((p.stdout+p.stderr).decode('utf8','replace'));raise RuntimeError(name+' failed')
        if name.endswith('-run'):print(p.stdout.decode('utf8','replace').strip())
    files=('firmware/main/core/speed_test_model.hpp','firmware/main/core/speed_test_store.hpp','firmware/main/core/speed_test_store.cpp','tooling/firmware/speed_test_model_test.cpp','tooling/firmware/speed_test_store_host.cpp')
    (run/'result.json').write_text(json.dumps({'passed':True,'stages':stages,'inputs':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files}},indent=2),encoding='utf8')
if __name__=='__main__':main()
