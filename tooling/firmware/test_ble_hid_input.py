"""Production HID mbuf ownership and allocation-failure replay. No device IO."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/validation/ble-hid-input')
    args=parser.parse_args(); out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    cpp=ROOT/'tooling/firmware/native/ble_hid_input_replay.cpp'
    sdk=Path(__import__('os').environ['IDF_PATH']) / 'components/esp_hid/src/nimble_hidd.c'
    digest=hashlib.sha256(sdk.read_bytes()).hexdigest()
    exe=out/'replay.exe'
    cmd=[str(Path(__import__('shutil').which('g++') or 'g++').resolve()),'-std=c++17','-Wall','-Wextra','-Werror',
         '-I',str(ROOT/'firmware'),str(cpp),'-o',str(exe)]
    env=os.environ.copy();env['PATH']=str(Path(__import__('shutil').which('g++') or 'g++').resolve().parent)+os.pathsep+env.get('PATH','')
    built=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',env=env)
    (out/'compile.log').write_text(built.stdout+built.stderr,encoding='utf-8');built.check_returncode()
    run=subprocess.run([str(exe)],capture_output=True,text=True,encoding='utf-8')
    (out/'replay.log').write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode()
    result=json.loads(run.stdout)
    assert digest==hashlib.sha256(sdk.read_bytes()).hexdigest()
    commands=json.loads((ROOT/'artifacts/firmware/compile_commands.json').read_text(encoding='utf-8'))
    hid=[Path(x['file']) for x in commands if Path(x['file']).name=='nimble_hidd.c']
    assert hid==[ROOT/'artifacts/firmware/yumo_adapters/nimble_hidd.c'],hid
    generated=hid[0].read_text(encoding='utf-8')
    assert str(ROOT/'firmware/cmake/nimble_hid_input.inc').replace('\\','/') in generated
    assert 'static int nimble_hidd_dev_input_set(' not in generated
    assert 'assert(' not in (ROOT/'firmware/cmake/nimble_hid_input.inc').read_text(encoding='utf-8')
    # A changed upstream function must stop configuration, never silently use
    # an adapter reviewed against a different SDK implementation.
    altered=out/'altered-sdk/src';altered.mkdir(parents=True,exist_ok=True)
    text=sdk.read_text(encoding='utf-8')
    start=text.index('static int nimble_hidd_dev_input_set(')
    text=text[:start]+text[start:].replace('    int rc;','    int rc; /* unreviewed change */',1)
    (altered/'nimble_hidd.c').write_text(text,encoding='utf-8')
    guard=out/'guard.cmake'
    guard.write_text('set(CONFIG_BT_NIMBLE_ENABLED TRUE)\nfunction(idf_component_get_property out component property)\n'
        'if(property STREQUAL "COMPONENT_DIR")\nset(${out} "'+altered.parent.as_posix()+'" PARENT_SCOPE)\n'
        'else()\nset(${out} "stub" PARENT_SCOPE)\nendif()\nendfunction()\n'
        'include("'+(ROOT/'firmware/cmake/nimble_hid_input.cmake').as_posix()+'")\n',encoding='utf-8')
    negative=subprocess.run([str(Path(__import__('shutil').which('cmake') or 'cmake').resolve()),'-P',str(guard)],
                            capture_output=True,text=True,encoding='utf-8')
    (out/'guard.log').write_text(negative.stdout+negative.stderr,encoding='utf-8')
    assert negative.returncode and 'ESP-IDF HID input changed' in negative.stderr
    result.update(sdk_modified=False,device_accessed=False,sdk_sha256=digest,
        actual_build_uses_adapter=True,unreviewed_sdk_change_rejected=True,
        generated_sha256=hashlib.sha256(hid[0].read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))
if __name__=='__main__':main()
