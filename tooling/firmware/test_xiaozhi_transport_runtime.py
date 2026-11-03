"""Replay production Xiaozhi transport with synthetic SDK, DNS, UDP and PSA faults."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    run = args.run_dir.resolve()
    if not run.is_relative_to((ROOT / 'artifacts/validation').resolve()):
        raise SystemExit('run-dir must be under artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    compiler = Path(__import__('shutil').which('g++') or 'g++').resolve()
    cjson = ROOT / 'dependencies/managed_components/espressif__cjson/cJSON'
    sources = ['firmware/main/core/xiaozhi_transport.hpp',
               'firmware/main/core/xiaozhi_transport.cpp',
               'firmware/main/core/xiaozhi_protocol.hpp',
               'firmware/main/core/xiaozhi_protocol.cpp',
               'firmware/main/core/xiaozhi_cloud.hpp',
               'tooling/firmware/native/xiaozhi_transport_replay.cpp',
               'tooling/firmware/test_xiaozhi_transport_runtime.py']
    hashes = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}
    source = (ROOT / 'firmware/main/core/xiaozhi_transport.cpp').read_text(encoding='utf-8')
    # Keep the entire implementation, replacing only includes with host stubs.
    (run / 'xiaozhi_transport.inc').write_text(
        re.sub(r'^\s*#include[^\n]*\n', '', source, flags=re.MULTILINE), encoding='utf-8')
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    exe = run / 'xiaozhi-transport.exe'
    def execute(name, command):
        p = subprocess.run(command, cwd=run, env=env, capture_output=True, timeout=120,
                           creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run / (name + '.log')).write_bytes(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError((p.stdout + p.stderr).decode(errors='replace')[-12000:])
        return p.stdout.decode().strip()
    execute('compile', [str(compiler), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-static-libgcc', '-static-libstdc++', '-pthread', '-I' + str(run),
                       '-I' + str(ROOT / 'firmware/main/core'), '-I' + str(cjson),
                       str(ROOT / 'tooling/firmware/native/xiaozhi_transport_replay.cpp'),
                       str(ROOT / 'firmware/main/core/xiaozhi_protocol.cpp'),
                       str(cjson / 'cJSON.c'), '-o', str(exe)])
    result = json.loads(execute('replay', [str(exe)]))
    result.update(inputs=hashes, device_access=False, network_access=False,
                  production_transport_and_protocol_compiled=True,
                  synthetic_sdk_dns_udp_crypto=True,
                  limits='Checks framing, bounds, cleanup and cancellation. Fake reversible cipher does not validate AES; no real TLS, broker, UDP audio or device timing exercised.')
    (run / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
