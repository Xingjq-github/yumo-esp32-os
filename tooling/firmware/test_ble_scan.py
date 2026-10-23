"""Actual scan callback replay; radio and time are synthetic."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);a=ap.parse_args()
 run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
 source=ROOT/'firmware/main/core/ble_hid.cpp';code=source.read_text(encoding='utf8')
 (run/'scan.inc').write_text(code[code.index('int scanEvent('):code.index('void clearPairRequest(')],encoding='utf8')
 (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n')
 cc=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
 native=ROOT/'tooling/firmware/native/ble_scan_test.cpp'
 for name,args in [('compile',[str(cc),'-std=c++17','-O1','-I'+str(run),'-I'+str(ROOT/'firmware/main'),str(native),'-static-libgcc','-static-libstdc++','-o',str(run/'scan.exe')]),('run',[str(run/'scan.exe')])]:
  p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
  (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
  assert p.returncode==0,p.stderr.decode(errors='replace')
 (run/'result.json').write_text(json.dumps(dict(passed=True,output=p.stdout.decode(),inputs={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [source,native]},physical_scan_tested=False),indent=2),encoding='utf8')
 print(p.stdout.decode())
if __name__=='__main__':main()
