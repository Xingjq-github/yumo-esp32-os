"""Replay production Xiaozhi parsers/builders against synthetic protocol data only."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    run = args.run_dir.resolve()
    if not run.is_relative_to((ROOT / 'artifacts/validation').resolve()):
        raise SystemExit('run-dir must be under artifacts/validation')
    run.mkdir(parents=True, exist_ok=True)
    compiler = Path(__import__('shutil').which('g++') or 'g++').resolve()
    cjson = ROOT / 'dependencies/managed_components/espressif__cjson/cJSON'
    environment = dict(os.environ)
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
    executable = run / 'xiaozhi-protocol.exe'
    command = [str(compiler), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
               '-DXIAOZHI_CLOUD_HOST_TEST', '-I' + str(ROOT / 'firmware/main'),
               '-I' + str(cjson), str(ROOT / 'tooling/firmware/native/xiaozhi_protocol_replay.cpp'),
               str(ROOT / 'firmware/main/core/xiaozhi_protocol.cpp'),
               str(ROOT / 'firmware/main/core/xiaozhi_cloud.cpp'), str(cjson / 'cJSON.c'),
               '-o', str(executable), '-static-libgcc', '-static-libstdc++']
    build = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', env=environment)
    (run / 'compile.log').write_text(build.stdout + build.stderr, encoding='utf-8')
    if build.returncode:
        print(build.stdout + build.stderr)
        raise SystemExit(build.returncode)
    replay = subprocess.run([str(executable)], capture_output=True, text=True, encoding='utf-8', errors='replace', env=environment)
    (run / 'replay.log').write_text(replay.stdout + replay.stderr, encoding='utf-8')
    if replay.returncode:
        print(replay.stdout + replay.stderr)
        raise SystemExit(replay.returncode)
    result = json.loads(replay.stdout)
    result.update({'production_sources': ['xiaozhi_protocol.cpp', 'xiaozhi_cloud.cpp'], 'network_access': False, 'device_access': False})
    (run / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
