"""Verify the actual NVS build copy and its upstream-change guard. No device IO."""
from pathlib import Path
import argparse, hashlib, json, subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    sdk=Path(__import__('os').environ['IDF_PATH']) / 'components/nvs_flash/src/nvs_page.cpp'
    original=sdk.read_text(encoding='utf-8')
    sdk_sha=hashlib.sha256(sdk.read_bytes()).hexdigest()
    assert sdk_sha=='e77a3cb5c9baa6011df2a0cb0053d7f84a003ac1d26d0a7845beadcfaefbfd49'
    old='findItem(item.nsIndex, item.datatype, item.key, findItemIndex, dupItem)'
    new='findItem(item.nsIndex, item.datatype, item.key, findItemIndex, dupItem, item.chunkIndex)'
    assert original.count(old)==1
    commands=json.loads((ROOT/'artifacts/firmware/compile_commands.json').read_text(encoding='utf-8'))
    page=[Path(x['file']) for x in commands if Path(x['file']).name=='nvs_page.cpp']
    expected=ROOT/'artifacts/firmware/yumo_adapters/nvs_page.cpp'
    assert page==[expected],page
    assert expected.read_text(encoding='utf-8')==original.replace(old,new,1)
    # A future upstream recovery change must stop configuration for review.
    altered=out/'altered-sdk/src';altered.mkdir(parents=True,exist_ok=True)
    marker='esp_err_t Page::mLoadEntryTable()'
    (altered/'nvs_page.cpp').write_text(original.replace(marker,marker+' /* changed upstream */',1),encoding='utf-8')
    cmake=ROOT/'firmware/cmake/nvs_blob_recovery.cmake'
    guard=out/'guard.cmake'
    guard.write_text('function(idf_component_get_property out component property)\n'
        'if(property STREQUAL "COMPONENT_DIR")\nset(${out} "'+altered.parent.as_posix()+'" PARENT_SCOPE)\n'
        'else()\nset(${out} "stub" PARENT_SCOPE)\nendif()\nendfunction()\n'
        'include("'+cmake.as_posix()+'")\n',encoding='utf-8')
    run=subprocess.run([str(Path(__import__('shutil').which('cmake') or 'cmake').resolve()),'-P',str(guard)],
                       capture_output=True,text=True,encoding='utf-8')
    (out/'guard.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    assert run.returncode and 'ESP-IDF NVS recovery changed' in run.stderr
    assert hashlib.sha256(sdk.read_bytes()).hexdigest()==sdk_sha
    result={'passed':True,'sdk_modified':False,'sdk_sha256':sdk_sha,
        'only_change':'Pass the last item chunk index to recovery duplicate lookup',
        'actual_build_uses_adapter':True,'unreviewed_sdk_change_rejected':True,
        'generated_sha256':hashlib.sha256(expected.read_bytes()).hexdigest(),'device_accessed':False}
    (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()
