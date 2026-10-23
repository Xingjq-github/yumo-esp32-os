"""Replay production boot radio scheduling and user cancellation without hardware."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True)
    run=ap.parse_args().run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    source=(ROOT/'main/core/system_service.cpp').read_text(encoding='utf-8')
    def part(a,b):return source[source.index(a):source.index(b,source.index(a))]
    (run/'boot.inc').write_text(part('void bootResult(', 'void refreshBluetooth('),encoding='utf-8')
    radio=(ROOT/'main/core/ble_hid.cpp').read_text(encoding='utf-8')
    (run/'boot_bond.inc').write_text(radio[radio.index('esp_err_t BleHid::bootBond('):radio.index('uintptr_t BleHid::privateLinkGeneration()')],encoding='utf-8')
    (run/'boot_state.inc').write_text(part('BluetoothConnectionFlow bluetoothFlow;', 'bool startsBluetoothConnection('),encoding='utf-8')
    (run/'enqueue.inc').write_text(part('esp_err_t enqueue(', 'esp_err_t syncTime();')+
        part('void SystemService::cancelBluetoothConnection()', 'void SystemService::cancelWifiScan()'),encoding='utf-8')
    flow=(ROOT/'../tooling/firmware/native/ble_flow_replay.cpp').read_text(encoding='utf-8').split('int main(){',1)[0]
    (run/'flow.inc').write_text(flow,encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n')
    (run/'esp_wifi_types.h').write_text('#pragma once\nstruct wifi_ap_record_t {};\n')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def execute(name,cmd):
        p=subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-7000:])
        return p.stdout.decode().strip()
    exe=run/'boot.exe'
    execute('compile',[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-I'+str(run),'-I'+str(ROOT/'main'),
        str(ROOT/'../tooling/firmware/native/boot_radio_replay.cpp'),'-o',str(exe),'-static-libgcc','-static-libstdc++'])
    output=execute('replay',[str(exe)])
    names=['main/core/system_service.cpp','main/core/system_service.hpp','main/core/ble_connection_flow.hpp','main/core/ble_hid.cpp','main/core/ble_hid.hpp',
        '../tooling/firmware/native/ble_flow_replay.cpp','../tooling/firmware/native/boot_radio_replay.cpp','../tooling/firmware/test_boot_radio_runtime.py']
    result=dict(passed=True,output=output,inputs={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names},
        hardware_accessed=False,limits='Production scheduler and connection flow; injected transport/readiness, no RF acceptance test')
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
