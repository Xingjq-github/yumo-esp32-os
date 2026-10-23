"""Replay production H-mode routing, focus, editors and clock keys against LVGL."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run-dir', type=Path, required=True)
    ap.add_argument('--c-cache', type=Path, required=True)
    args = ap.parse_args()
    run, cache = args.run_dir.resolve(), args.c_cache.resolve()
    for path in (run, cache):
        assert path.is_relative_to((ROOT / 'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    sources = {}
    # Reuse the transport doubles, but run the production discovery/profile and
    # decoder all the way through to LVGL. No hard-coded H-mode getter.
    helper = ROOT / 'tooling/firmware/native/ble_gamepad_test.cpp'
    harness = helper.read_text(encoding='utf-8')
    sources[str(helper.relative_to(ROOT))] = sha(helper)
    (run / 'ble_transport.inc').write_text(harness[harness.index('struct ble_uuid_t'):harness.index('static void logInfo')], encoding='utf-8')
    (run / 'ble_discovery.inc').write_text(harness[harness.index('static ble_gatt_error ok'):harness.index('int main(){')], encoding='utf-8')
    fixture = ROOT / 'tooling/firmware/fixtures/iine-l1162-h-20261022.json'
    sources[str(fixture.relative_to(ROOT))] = sha(fixture)
    report_map = bytes.fromhex(json.loads(fixture.read_text(encoding='utf-8'))['report_map_hex'])
    (run / 'h_fixture.inc').write_text('static const std::vector<uint8_t> hMap={' + ','.join(map(str, report_map)) + '};\n', encoding='utf-8')
    for name, target in (('core/ble_keyboard_client', 'ble_client'), ('core/event_bus', 'event_bus')):
        path = ROOT / ('firmware/main/' + name + '.cpp')
        sources[str(path.relative_to(ROOT))] = sha(path)
        (run / (target + '.inc')).write_text(re.sub(r'^#include .*\n', '', path.read_text(encoding='utf-8'), flags=re.M), encoding='utf-8')
    for name in ('ui/external_keyboard', 'ui/text_input', 'ui/bluetooth_pairing', 'ui/compact_ui'):
        path = ROOT / ('firmware/main/' + name + '.cpp')
        sources[str(path.relative_to(ROOT))] = sha(path)
        (run / (path.stem + '.inc')).write_text(re.sub(r'^#include .*\n', '', path.read_text(encoding='utf-8'), flags=re.M), encoding='utf-8')
    for name, signatures in {
        'ui/window_mgr': ('lv_obj_t *WindowManager::inputRoot()', 'bool WindowManager::directionalKey(', 'void WindowManager::goBack('),
        'apps/watchface': ('void Watchface::changeFace(', 'bool Watchface::onDirectionalKey('),
        'ui/quick_panel': ('bool QuickPanel::allowEvent(',),
    }.items():
        path = ROOT / ('firmware/main/' + name + '.cpp')
        sources[str(path.relative_to(ROOT))] = sha(path)
        namespace = 'ui' if name.startswith('ui/') else 'apps'
        text = 'namespace ' + namespace + ' {\n' + ''.join(function(path.read_text(encoding='utf-8'), s) for s in signatures) + '}\n'
        (run / (path.stem + '.inc')).write_text(text, encoding='utf-8')
    (run / 'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 259\n#define ESP_ERR_NOT_FOUND 261\n#define ESP_ERR_TIMEOUT 263\n')
    (run / 'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    shutil.copyfile(cache / 'lv_conf.h', run / 'lv_conf.h')
    manifest = json.loads((cache / 'c-objects.json').read_text())
    objects = []
    for name, digest in manifest['inputs'].items():
        assert sha(cache / name if name == 'lv_conf.h' else ROOT / 'firmware' / name) == digest, name
    for name, digest in manifest['objects'].items():
        assert sha(cache / name) == digest, name
        shutil.copyfile(cache / name, run / name)
        objects.append(run / name)
    cc = Path(__import__('shutil').which('g++') or 'g++').resolve()
    env = dict(os.environ)
    env['PATH'] = str(cc.parent) + os.pathsep + env['PATH']
    def execute(name, arguments, compiler=cc):
        rsp = run / (name + '.rsp')
        rsp.write_text('\n'.join('"' + str(v).replace('\\', '/') + '"' for v in arguments), encoding='utf-8')
        p = subprocess.run([str(compiler), '@' + str(rsp)], cwd=run, env=env, capture_output=True, timeout=120, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run / (name + '.log')).write_bytes(p.stdout + p.stderr)
        assert p.returncode == 0, p.stderr.decode(errors='replace')[-8000:]
    lv = ROOT / 'dependencies/managed_components/lvgl__lvgl'
    flags = ['-O1', '-DLV_FONT_MONTSERRAT_10=1', '-DLV_CONF_INCLUDE_SIMPLE', '-I' + str(run), '-I' + str(lv), '-I' + str(ROOT / 'firmware/main'), '-I' + str(ROOT / 'firmware/main/ui')]
    execute('font', ['-std=c11'] + flags + ['-c', lv / 'src/font/lv_font_montserrat_10.c'], Path(__import__('shutil').which('gcc') or 'gcc').resolve())
    native = ROOT / 'tooling/firmware/native/controller_navigation_replay.cpp'
    execute('compile', ['-std=c++17'] + flags + ['-c', native, ROOT / 'firmware/main/ui/theme.cpp'])
    exe = run / 'controller-navigation.exe'
    execute('link', ['-static-libgcc', '-static-libstdc++', '-o', exe] + objects + [run / 'controller_navigation_replay.o', run / 'theme.o'])
    p = subprocess.run([str(exe)], cwd=run, env=env, capture_output=True, timeout=60, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run / 'run.log').write_bytes(p.stdout + p.stderr)
    assert p.returncode == 0, (p.stdout + p.stderr).decode(errors='replace')[-6000:]
    result = json.loads(p.stdout)
    result.update(sources=sources, native_sha256=sha(native), device_accessed=False)
    (run / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(p.stdout.decode().strip())

if __name__ == '__main__':
    main()
