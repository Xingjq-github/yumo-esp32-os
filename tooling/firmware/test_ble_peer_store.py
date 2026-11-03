"""Replay the production peer metadata store with an in-memory NVS transport."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run-dir', type=Path, required=True)
    args = ap.parse_args()
    run = args.run_dir.resolve()
    assert run.is_relative_to(ROOT / 'artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'firmware/main/core/ble_peer_store.cpp'
    native = ROOT / 'tooling/firmware/native/ble_peer_store_replay.cpp'
    (run / 'peer_store.inc').write_text(re.sub(r'^#include .*\n', '', source.read_text(encoding='utf-8'), flags=re.M), encoding='utf-8')
    (run / 'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 259\n#define ESP_ERR_NOT_FOUND 261\n#define ESP_ERR_TIMEOUT 263\n')
    for name in ('sdkconfig', 'sdkconfig.defaults'):
        config = (ROOT / 'firmware' / name).read_text(encoding='utf-8')
        assert re.search(r'^CONFIG_BT_NIMBLE_MAX_BONDS=16$', config, re.M)
        assert re.search(r'^CONFIG_BT_NIMBLE_MAX_CCCDS=128$', config, re.M)
    cc = Path(__import__('shutil').which('g++') or 'g++').resolve()
    env = dict(os.environ)
    env['PATH'] = str(cc.parent) + os.pathsep + env['PATH']
    commands = [('compile', [cc, '-std=c++17', '-O1', '-I'+str(run), '-I'+str(ROOT/'firmware/main'), native, '-static-libgcc', '-static-libstdc++', '-o', run/'test.exe']), ('run', [run/'test.exe'])]
    for name, args in commands:
        p = subprocess.run(list(map(str, args)), cwd=run, env=env, capture_output=True, timeout=90, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run / (name+'.log')).write_bytes(p.stdout+p.stderr)
        assert p.returncode == 0, (p.stdout+p.stderr).decode(errors='replace')[-5000:]
    files = [source, native, Path(__file__), ROOT/'firmware/sdkconfig', ROOT/'firmware/sdkconfig.defaults', ROOT/'firmware/main/core/system_service.hpp', ROOT/'firmware/main/core/ble_peer_store.hpp']
    (run/'result.json').write_text(json.dumps(dict(passed=True, output=p.stdout.decode(), device_accessed=False, inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}), indent=2), encoding='utf-8')
    print(p.stdout.decode())

if __name__ == '__main__':
    main()
