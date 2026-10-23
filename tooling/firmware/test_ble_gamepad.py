"""Replay the actual HID parser and BLE discovery against synthetic peripherals."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);a=ap.parse_args()
 run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
 cc=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
 source=ROOT/'firmware/main/core/ble_keyboard_client.cpp'
 fixture=json.loads((ROOT/'tooling/firmware/fixtures/iine-l1162-h-20261022.json').read_text(encoding='utf8'))
 report_map=bytes.fromhex(fixture['report_map_hex'])
 fixture_cpp='static const std::vector<uint8_t> iineMap={'+','.join(str(v) for v in report_map)+'};\n'
 fixture_cpp+='static const struct {std::array<uint8_t,8> bytes;uint8_t pad;} iineSamples[]={\n'
 for sample in fixture['pressed_samples']:
  data=bytes.fromhex(sample['report_hex']);assert len(data)==8
  fixture_cpp+='{{'+','.join(str(v) for v in data)+'},'+str(sample['expected_fc_pad'])+'},\n'
 fixture_cpp+='};\n'
 c_fixture=json.loads((ROOT/'tooling/firmware/fixtures/iine-l1162-c-20261022.json').read_text(encoding='utf8'))
 fixture_cpp+='static const std::vector<uint8_t> iineCMap={'+','.join(str(v) for v in bytes.fromhex(c_fixture['report_map_hex']))+'};\n'
 fixture_cpp+='static const struct {std::array<uint8_t,16> bytes;uint8_t pad;} iineCSamples[]={\n'
 for sample in c_fixture['samples']:
  data=bytes.fromhex(sample['report_hex']);assert len(data)==16
  fixture_cpp+='{{'+','.join(str(v) for v in data)+'},'+str(sample['expected_fc_pad'])+'},\n'
 fixture_cpp+='};\n'
 (run/'iine_fixture.inc').write_text(fixture_cpp,encoding='utf8')
 (run/'ble_client.inc').write_text(re.sub(r'^#include .*\n','',source.read_text(encoding='utf8'),flags=re.M),encoding='utf8')
 def execute(name,args):
  p=subprocess.run([str(x) for x in args],cwd=run,env=env,capture_output=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
  (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
  if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-5000:])
  return p.stdout.decode()
 execute('compile',[cc,'-std=c++17','-O1','-Wall','-Wextra','-I'+str(ROOT/'firmware/main'),'-I'+str(run),ROOT/'tooling/firmware/native/ble_gamepad_test.cpp','-static-libgcc','-static-libstdc++','-o',run/'test.exe'])
 output=execute('run',[run/'test.exe'])
 files=['firmware/main/core/ble_keyboard_client.cpp','firmware/main/core/ble_keyboard_client.hpp','firmware/main/core/hid_input_report.hpp','firmware/main/core/hid_gamepad_report.hpp','firmware/main/core/hid_iine_profile.hpp','firmware/main/core/gamepad_input.hpp','tooling/firmware/native/ble_gamepad_test.cpp','tooling/firmware/fixtures/iine-l1162-h-20261022.json','tooling/firmware/fixtures/iine-l1162-c-20261022.json']
 (run/'result.json').write_text(json.dumps(dict(passed=True,output=output,physical_controller_tested=False,inputs={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}),indent=2),encoding='utf8');print(output)
if __name__=='__main__':main()
