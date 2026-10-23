"""Replay production SNTP orchestration with a fake network/clock; no device access."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir',required=True,type=Path)
    args=ap.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    source=(ROOT/'main/core/system_service.cpp').read_text(encoding='utf-8')
    header=(ROOT/'main/core/system_service.hpp').read_text(encoding='utf-8')
    snapshot=header[header.index('enum class TimeSyncState'):header.index('struct BluetoothSnapshot')]
    state=source[source.index('bool sntpStarted=false;'):source.index('BluetoothConnectionFlow bluetoothFlow;')]
    functions=source[source.index('esp_err_t syncTime();'):source.index('esp_err_t execute(')]
    # Keep all production control flow. Only clock syscalls are redirected.
    functions=functions.replace('time(nullptr)','fakeWallTime(nullptr)').replace('settimeofday(', 'fakeSetTime(')
    # Windows timeval is 32-bit; ESP-IDF's timeval uses 64-bit time_t.
    functions=functions.replace('timeval tv', 'FakeTimeval tv')
    (run/'network_time.inc').write_text(snapshot+state+functions,encoding='utf-8')
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    flags=dict(cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
    p=subprocess.run([str(cc),'-std=c++17','-O1','-Wall','-Wextra','-static-libgcc','-static-libstdc++',
                      '-I'+str(run),str(ROOT/'../tooling/firmware/native/network_time_replay.cpp'),'-o',str(run/'replay.exe')],**flags)
    (run/'compile.log').write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
    p=subprocess.run([str(run/'replay.exe')],**flags)
    (run/'replay.log').write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
    result=dict(passed=True,result=p.stdout.decode().strip(),physical_ntp_test=False,device_accessed=False,
                inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
                        ('main/core/system_service.cpp','main/core/system_service.hpp','../tooling/firmware/native/network_time_replay.cpp')})
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
