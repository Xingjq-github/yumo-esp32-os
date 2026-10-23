"""Replay real input/window lifecycle code in LVGL; hardware services are fakes.

Reuses hash-checked local LVGL host objects. No firmware build or device access.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess

ROOT = (Path(__file__).resolve().parents[2]/'firmware')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', type=Path, required=True)
    ap.add_argument('--run-dir', type=Path, required=True)
    ap.add_argument('--c-cache', type=Path, required=True)
    a = ap.parse_args()
    a.compiler = a.compiler.resolve()
    run, cache = a.run_dir.resolve(), a.c_cache.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    assert cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((cache / 'c-objects.json').read_text())
    changed = []
    for name, checksum in manifest['inputs'].items():
        source = cache / name if name == 'lv_conf.h' else ROOT / name
        if sha(source) != checksum:
            assert source == ROOT / 'main/ui/lv_font_menu_14.c', str(source)
            changed.append(source)
    objects = []
    for name, checksum in manifest['objects'].items():
        assert sha(cache / name) == checksum, name
        shutil.copyfile(cache / name, run / name)
        objects.append(run / name)
    shutil.copyfile(cache / 'lv_conf.h', run / 'lv_conf.h')
    (run / 'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_TIMEOUT 0x107\n')
    (run / 'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    (run / 'esp_wifi_types.h').write_text('#pragma once\nstruct wifi_ap_record_t {};\n')
    inputs = {}
    for name in ('text_input', 'compact_ui', 'window_mgr'):
        path = ROOT / 'main/ui' / (name + '.cpp')
        inputs[str(path.relative_to(ROOT))] = sha(path)
        code = re.sub(r'^#include .*\n', '', path.read_text(encoding='utf-8'), flags=re.M)
        (run / (name + '.inc')).write_text(code, encoding='utf-8')
    cpp = [ROOT / '../tooling/firmware/native/app_lifecycle_replay.cpp', ROOT / 'main/ui/theme.cpp']
    for path in cpp:
        inputs[str(path.relative_to(ROOT))] = sha(path)
    env = dict(os.environ)
    env['PATH'] = str(a.compiler.resolve().parent) + os.pathsep + env.get('PATH', '')

    def execute(name, args):
        p = subprocess.run(args, cwd=run, env=env, capture_output=True, timeout=120,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        (run / (name + '.log')).write_bytes(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError((p.stdout + p.stderr).decode(errors='replace')[-6000:])
        return p.stdout.decode(errors='replace')

    def rsp(name, args):
        path = run / (name + '.rsp')
        path.write_text('\n'.join('"' + str(x).replace('\\', '/') + '"' for x in args))
        return '@' + str(path)

    common = ['-O1', '-std=c++17', '-DLV_CONF_INCLUDE_SIMPLE', '-I' + str(run),
              '-I' + str(ROOT / '../dependencies/managed_components/lvgl__lvgl'),
              '-I' + str(ROOT / 'main'), '-I' + str(ROOT / 'main/ui')]
    if changed:
        execute('font', [str(a.compiler.with_name('gcc.exe')),
                        rsp('font', [x for x in common if x != '-std=c++17']
                            + ['-std=c11', '-c'] + changed)])
    execute('compile', [str(a.compiler), rsp('compile', common + ['-c'] + cpp)])
    exe = run / 'app_lifecycle.exe'
    execute('link', [str(a.compiler), rsp('link', ['-static-libgcc', '-static-libstdc++', '-o', exe]
            + objects + [run / 'app_lifecycle_replay.o', run / 'theme.o'])])
    output = execute('execute', [str(exe)])
    result = dict(passed=True, result=output.strip(), inputs=inputs,
                  method='real LVGL and production TextInput/WindowManager/compact/theme; probe apps and fake hardware',
                  device_accessed=False, firmware_built=False)
    (run / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(output.strip())


if __name__ == '__main__':
    main()
