from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
 run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
 source=ROOT/'firmware/main/core/speed_test.cpp';text=source.read_text(encoding='utf8')
 (run/'production_measure.inc').write_text('void measure('+text.split('void measure(',1)[1].split('\nvoid persist(',1)[0],encoding='utf8')
 cc=a.compiler.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
 harness=ROOT/'tooling/firmware/speed_test_measure_host.cpp'
 for stage,cmd in [('compile',[str(cc),'-std=c++17','-O1','-I'+str(ROOT/'firmware/main'),'-I'+str(run),str(harness),'-o',str(run/'measure.exe')]),('execute',[str(run/'measure.exe')])]:
  r=subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=120);(run/(stage+'.log')).write_bytes(r.stdout+r.stderr)
  if r.returncode:print((r.stdout+r.stderr).decode('utf8','replace'));raise RuntimeError(stage+' failed')
 result=dict(passed=True,method='Production measurement scheduler; deterministic transport and probes, no hardware claim',output=r.stdout.decode().strip(),inputs={f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in (source,harness,ROOT/'firmware/main/core/speed_test.hpp',ROOT/'firmware/main/core/speed_test_model.hpp')})
 (run/'result.json').write_text(json.dumps(result,indent=2));print(result['output'])
if __name__=='__main__':main()