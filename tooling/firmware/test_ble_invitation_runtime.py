"""Run actual invitation functions and SDK disconnect handling with fake HCI."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
SDK=Path(__import__('os').environ['IDF_PATH'])

def section(s,start,end):return s[s.index(start):s.index(end,s.index(start)+len(start))]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir',type=Path,required=True)
    ap.add_argument('--case',choices=('invitation','sdk-retry-on','sdk-retry-off','pairing','flow'))
    args=ap.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    capacity=re.search(r'^CONFIG_BT_NIMBLE_MAX_BONDS=(\d+)$',(ROOT/'sdkconfig').read_text(encoding='utf-8'),re.M).group(1)
    (run/'bond_capacity.inc').write_text('constexpr int kMaxBondSlots='+capacity+';\n',encoding='utf-8')
    cc=ROOT/'../dependencies/host/w64devkit/bin/g++.exe'
    source=(ROOT/'main/core/ble_hid.cpp').read_text(encoding='utf-8')
    snippets=[section(source,'void cancelDiscoverableTimeout(void)','// One report map'),
              section(source,'esp_err_t BleHid::reconnectBond(', 'esp_err_t BleHid::startScan('),
              section(source,'esp_err_t BleHid::makeDiscoverable(', 'void BleHid::cancelUserScan('),
              section(source,'namespace {\nvoid processAdvertising()', 'esp_err_t BleHid::prepareDeepSleep()')]
    (run/'invitation.inc').write_text('\n'.join(snippets),encoding='utf-8')
    config=section(source,'esp_err_t configureGap(void)','    // Just Works pairing:')
    (run/'adv_config.inc').write_text(config+'return ESP_OK;\n}\n',encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n',encoding='utf-8')
    sdk_path=SDK/'components/bt/host/nimble/nimble/nimble/host/src/ble_hs_hci_evt.c'
    sdk=sdk_path.read_text(encoding='utf-8')
    start=sdk.index('static int\nble_hs_hci_evt_disconn_complete(')
    end=sdk.index('\n}\n',start)+3
    (run/'sdk_disconnect.inc').write_text(sdk[start:end],encoding='utf-8')
    (run/'pairing.inc').write_text(section(source,'bool completeSecureConnection(', 'esp_err_t configureGap('),encoding='utf-8')
    (run/'connection_lifecycle.inc').write_text(section(source,'esp_err_t BleHid::disconnect(', 'bool BleHid::keyboardConnected('),encoding='utf-8')
    radio=(ROOT/'../tooling/firmware/native/ble_invitation_replay.cpp').read_text(encoding='utf-8').split('int main(){',1)[0]
    radio=radio.replace('struct ble_gap_event{};','').replace('int gapEvent(ble_gap_event *,void*){return 0;}','int gapEvent(ble_gap_event *,void*);')
    (run/'pairing_radio.inc').write_text(radio,encoding='utf-8')
    sdk_sources={str(sdk_path):hashlib.sha256(sdk_path.read_bytes()).hexdigest()}
    for file,marker,output in [('ble_gap.c','void\nble_gap_conn_broken(', 'sdk_gap_broken.inc'),
                               ('ble_sm.c','void\nble_sm_connection_broken(', 'sdk_sm_broken.inc')]:
        path=sdk_path.parent/file;text=path.read_text(encoding='utf-8')
        begin=text.index(marker);end=text.index('\n}\n',begin)+3
        (run/output).write_text(text[begin:end],encoding='utf-8')
        sdk_sources[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def execute(name,cmd):
        p=subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-6000:])
        return p.stdout.decode().strip()
    outputs=[]
    for name,file,flags in [('invitation','ble_invitation_replay.cpp',[]),
                            # The unchanged SDK function is C (implicit void*
                            # conversion); permit that conversion in this C++ harness.
                            ('sdk-retry-on','ble_sdk_disconnect_replay.cpp',['-fpermissive','-DRETRY_ENABLED=1']),
                            ('sdk-retry-off','ble_sdk_disconnect_replay.cpp',['-fpermissive','-DRETRY_ENABLED=0']),
                            ('pairing','ble_pairing_replay.cpp',[]),
                            ('flow','ble_flow_replay.cpp',[])]:
        if args.case and args.case!=name:continue
        exe=run/(name+'.exe')
        execute(name+'-compile',[str(cc),'-std=c++17','-O1','-g','-static-libgcc','-static-libstdc++',
                                '-I'+str(run),'-I'+str(ROOT/'main'),*flags,str(ROOT/'../tooling/firmware/native'/file),'-o',str(exe)])
        outputs.append(execute(name,[str(exe)]))
    inputs={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in (
        'sdkconfig','main/core/ble_hid.cpp','main/core/ble_hid.hpp','main/core/ble_peer_identity.hpp',
        '../tooling/firmware/test_ble_invitation_runtime.py','../tooling/firmware/native/ble_invitation_replay.cpp',
        '../tooling/firmware/native/ble_sdk_disconnect_replay.cpp','../tooling/firmware/native/ble_pairing_replay.cpp',
        'main/core/ble_connection_flow.hpp','../tooling/firmware/native/ble_flow_replay.cpp')}
    result=dict(passed=True,selected_case=args.case or 'all',outputs=outputs,inputs=inputs,sdk_source=str(sdk_path),
                sdk_sha256=hashlib.sha256(sdk_path.read_bytes()).hexdigest(),
                sdk_sources=sdk_sources,synthetic_hci=True,device_accessed=False,
                limits='Actual invitation and SDK event functions; fake controller/IRK, not an iPhone or RF test')
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('\n'.join(outputs))

if __name__=='__main__':main()
