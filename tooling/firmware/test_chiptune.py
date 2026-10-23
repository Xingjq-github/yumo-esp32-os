"""Run actual production score parser and DSP on the host; no device access."""
from pathlib import Path
import argparse,os,subprocess,json,hashlib
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def execute(name,args):
        result=subprocess.run([str(v) for v in args],cwd=run,env=env,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-8000:])
        return result.stdout.decode('utf-8')
    cj=ROOT/'../dependencies/managed_components/espressif__cjson/cJSON'
    execute('cjson',[cc.with_name('gcc.exe'),'-O2','-c',cj/'cJSON.c','-o','cJSON.o'])
    execute('compile',[cc,'-std=c++17','-O2','-Wall','-Wextra','-I'+str(cj),'-I'+str(ROOT/'main'),ROOT/'../tooling/firmware/native/chiptune_test.cpp',ROOT/'main/core/chiptune_score.cpp','cJSON.o','-o','score.exe','-static-libgcc','-static-libstdc++'])
    scores=sorted((ROOT/'../resources/sdcard/chiptune').glob('*.chip.json'))
    output=execute('run',[run/'score.exe',*scores])
    inputs=[ROOT/'../tooling/firmware/native/chiptune_test.cpp',ROOT/'main/core/chiptune_score.cpp',ROOT/'main/core/chiptune_score.hpp',*scores]
    result=dict(passed=True,output=output,inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},hardware_audio_tested=False)
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
