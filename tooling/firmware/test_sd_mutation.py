from pathlib import Path
import argparse,json,os,re,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',required=True);a=p.parse_args();run=Path(a.run_dir).resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    code=(ROOT/'main/core/sd_media.cpp').read_text(encoding='utf-8')
    (run/'sd_media.inc').write_text(re.sub(r'^#include .*\n','',code,flags=re.M),encoding='utf-8')
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH'];exe=run/'sd.exe'
    args=[str(cc),'-std=c++17','-O2','-static-libgcc','-static-libstdc++','-I'+str(ROOT/'main'),'-I'+str(run),str(ROOT/'../tooling/firmware/native/sd_mutation_replay.cpp'),'-o',str(exe)]
    result=subprocess.run(args,capture_output=True,env=env);(run/'compile.log').write_bytes(result.stdout+result.stderr);assert not result.returncode,result.stderr.decode()
    result=subprocess.run([str(exe)],capture_output=True,env=env);(run/'run.log').write_bytes(result.stdout+result.stderr);assert not result.returncode,result.stderr.decode()
    data=json.loads(result.stdout);(run/'result.json').write_text(json.dumps(data,indent=2));print(json.dumps(data))
if __name__=='__main__':main()
