"""Execute production discovery/activation against synthetic HTTP, NVS and lifecycle adapters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--run-dir',type=Path,required=True)
    run=parser.parse_args().run_dir.resolve()
    if not run.is_relative_to((ROOT/'artifacts/validation').resolve()):
        raise SystemExit('run-dir must be under artifacts/validation')
    run.mkdir(parents=True,exist_ok=True)
    source=ROOT/'firmware/main/core/xiaozhi_cloud.cpp'
    text=source.read_text(encoding='utf-8')
    # No method/body rewrites: only SDK include resolution is replaced by host adapters.
    text=re.sub(r'^#include .*\n','',text,flags=re.M)
    (run/'xiaozhi_cloud.inc').write_text(text,encoding='utf-8')
    compiler=Path(__import__('shutil').which('g++') or 'g++').resolve()
    cjson=ROOT/'dependencies/managed_components/espressif__cjson/cJSON'
    environment=dict(os.environ)
    environment['PATH']=str(compiler.parent)+os.pathsep+environment.get('PATH','')
    executable=run/'xiaozhi-cloud.exe'
    command=[str(compiler),'-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(run),
        '-I'+str(ROOT/'firmware/main'),'-I'+str(cjson),
        str(ROOT/'tooling/firmware/native/xiaozhi_cloud_replay.cpp'),
        str(ROOT/'firmware/main/core/xiaozhi_protocol.cpp'),str(cjson/'cJSON.c'),
        '-o',str(executable),'-static-libgcc','-static-libstdc++']
    build=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',env=environment)
    (run/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:
        print(build.stdout+build.stderr)
        raise SystemExit(build.returncode)
    replay=subprocess.run([str(executable)],capture_output=True,text=True,encoding='utf-8',errors='replace',env=environment)
    (run/'replay.log').write_text(replay.stdout+replay.stderr,encoding='utf-8')
    if replay.returncode:
        print(replay.stdout+replay.stderr)
        raise SystemExit(replay.returncode)
    result=json.loads(replay.stdout)
    result.update({'production_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'network_access':False,'device_access':False,'production_discover_request':True})
    (run/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()
