"""Execute production Wi-Fi lifecycle with injected IDF failures; no device access."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT = (Path(__file__).resolve().parents[2]/'firmware')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    run, compiler = args.run_dir.resolve(), args.compiler.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    inputs = {}
    for path in ('main/core/connectivity.cpp', 'main/core/connectivity.hpp',
                 '../tooling/firmware/native/wifi_lifecycle_replay.cpp', '../tooling/firmware/test_wifi_lifecycle_runtime.py'):
        inputs[path] = hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
    source = (ROOT/'main/core/connectivity.cpp').read_text(encoding='utf-8')
    (run/'connectivity.inc').write_text(re.sub(r'^#include .*\n', '', source, flags=re.M), encoding='utf-8')
    for name in ('esp_err.h', 'esp_wifi_types.h'):
        (run/name).write_text('#pragma once\n', encoding='utf-8')
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent)+os.pathsep+env.get('PATH', '')
    def execute(name, command):
        result = subprocess.run(command, cwd=run, env=env, capture_output=True,
                                timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-7000:])
        return result.stdout.decode().strip()
    exe = run/'wifi-lifecycle.exe'
    execute('compile', [str(compiler), '-std=c++17', '-O1', '-g', '-static-libgcc', '-static-libstdc++',
                       '-I'+str(run), '-I'+str(ROOT/'main'),
                       str(ROOT/'../tooling/firmware/native/wifi_lifecycle_replay.cpp'), '-o', str(exe)])
    output = execute('replay', [str(exe)])
    result = dict(passed=True, output=output, inputs=inputs, synthetic_idf=True, device_accessed=False,
                  limits='No radio throughput, real internal heap measurement, or RF coexistence test')
    (run/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(output)

if __name__ == '__main__':
    main()
