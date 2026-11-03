#!/usr/bin/env python3
"""Production bond backend against the pinned SDK's real NVS implementation."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from test_nvs_capacity_host import build_host

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--page-source',type=Path,help='Exact generated production NVS recovery source')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    inc=out/'nimble_include'
    for name in ['syscfg','host','os']:(inc/name).mkdir(parents=True,exist_ok=True)
    values={'BLE_STATIC_TO_DYNAMIC':1,'BLE_STORE_CONFIG_PERSIST':1,'BLE_HOST_BASED_PRIVACY':0,
            'ENC_ADV_DATA':0,'BLE_STORE_MAX_BONDS':16,'BLE_STORE_MAX_CCCDS':128,'BLE_STORE_MAX_CSFCS':16,
            'BLE_GATT_CSFC_SIZE':1,'BLE_MULTI_ADV_INSTANCES':0,'BLE_LL_CFG_FEAT_LL_PRIVACY':0,
            'BLE_LL_CFG_FEAT_LL_EXT_ADV':0}
    (inc/'syscfg/syscfg.h').write_text('#pragma once\n#define MYNEWT_VAL(x) MYNEWT_VAL_ ## x\n'+
        ''.join(f'#define MYNEWT_VAL_{k} {v}\n' for k,v in values.items()),encoding='utf-8')
    (inc/'os/os.h').write_text('#pragma once\n',encoding='utf-8')
    # Struct declarations come from the actual SDK ble_store.h and ble.h; only
    # the wider host task/runtime API is excluded from this storage test.
    header=Path(__import__('os').environ['IDF_PATH']) / 'components/bt/host/nimble/nimble/nimble/host/include/host/ble_hs.h'
    error=re.search(r'#define BLE_HS_ESTORE_FAIL\s+(\d+)',header.read_text()).group(1)
    (inc/'host/ble_hs.h').write_text('#pragma once\n#include "host/ble_store.h"\n#define BLE_HS_ESTORE_FAIL '+error+'\n',encoding='utf-8')
    nimble=Path(__import__('os').environ['IDF_PATH']) / 'components/bt/host/nimble/nimble/nimble'
    component=root/'firmware/components/yumo_ble_store'
    sources=[root/'tooling/firmware/native/ble_bond_store_replay.cpp',component/'yumo_ble_store.cpp']
    exe,env,receipt=build_host(root,out,sources,name='ble_bond_store',include_dirs=[inc,component/'include',
        nimble/'include',nimble/'host/include',nimble/'host/store/config/src'],
        sdk_source_overrides={'nvs_page.cpp':args.page_source.resolve()} if args.page_source else None)
    profiles=[]
    for profile in ['maximum','typical']:
        p=subprocess.run([str(exe),profile],capture_output=True,env=env,timeout=180)
        (out/(profile+'-run.log')).write_bytes(p.stdout+p.stderr)
        print((p.stdout+p.stderr).decode(errors='replace'))
        if p.returncode:raise SystemExit(p.returncode)
        profiles.append(json.loads(p.stdout.decode().strip().splitlines()[-1]))
    result={'passed':True,'profiles':profiles,'build_receipt':str(receipt)}
    result['source_sha256']={(str(x.relative_to(root)) if x.is_relative_to(root) else '@sdk/'+x.relative_to(Path(__import__('os').environ['IDF_PATH'])).as_posix()):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources+[Path(__file__)]}
    result['limitations']=['Synthetic keys only; no device NVS read or physical power-loss validation.',
      'OS concurrency is serialized; raw NOR write/erase prefixes are deterministic, not analog flash physics.',
      'Unknown historical namespaces can exhaust fixed 16KiB; migration fails closed without erasing existing keys.',
      'Maximum profile verifies BLE/metadata churn but excludes WiFi-history string churn after confirming its existing contiguous-page limitation; typical profile includes history churn.']
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
