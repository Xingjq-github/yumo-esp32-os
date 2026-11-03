"""Run the production keyboard/mouse App with real LVGL pointer events.

BLE/system services are doubles: this validates GUI event handling and resource
lifetime, not radio reliability, real touch latency or host cursor response.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--c-cache', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    run, cache = args.run_dir.resolve(), args.c_cache.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    assert cache.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True, exist_ok=args.resume)
    compiler = args.compiler.resolve()
    firmware, main_dir = ROOT/'firmware', ROOT/'firmware/main'
    manifest = json.loads((cache/'c-objects.json').read_text())
    changed, objects, inputs = [], [], {}
    for name, digest in manifest['inputs'].items():
        path = cache/name if name == 'lv_conf.h' else firmware/name
        if sha(path) != digest:
            assert path.resolve() in ((main_dir/'ui/lv_font_menu_14.c').resolve(),
                                      (main_dir/'ui/lv_font_app_icons.c').resolve()), name
            changed.append(path)
        inputs[str(path.resolve())] = sha(path)
    for name, digest in manifest['objects'].items():
        assert sha(cache/name) == digest, name
        if not (run/name).exists() or sha(run/name) != digest:
            shutil.copyfile(cache/name, run/name)
        objects.append(run/name)
    shutil.copyfile(cache/'lv_conf.h', run/'lv_conf.h')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n'
        'constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NO_MEM=0x101,ESP_ERR_TIMEOUT=0x107,ESP_ERR_NOT_FOUND=0x105;\n', encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n'
        '#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n', encoding='utf-8')
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent)+os.pathsep+env.get('PATH', '')

    def execute(name, exe, values):
        response = run/(name+'.rsp')
        response.write_text('\n'.join('"'+str(v).replace('\\', '/')+'"' for v in values), encoding='utf-8')
        outcome = subprocess.run([str(exe), '@'+str(response)] if values else [str(exe)],
                                 cwd=run, env=env, capture_output=True, timeout=180,
                                 creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(outcome.stdout+outcome.stderr)
        if outcome.returncode:
            raise RuntimeError((outcome.stdout+outcome.stderr).decode('utf-8', 'replace')[-12000:])
        return outcome.stdout.decode('utf-8', 'replace')

    flags = ['-O1', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_FONT_MONTSERRAT_10=1', '-DLV_FONT_MONTSERRAT_12=1', '-I'+str(run),
             '-I'+str(ROOT/'dependencies/managed_components/lvgl__lvgl'),
             '-I'+str(main_dir), '-I'+str(main_dir/'ui'), '-I'+str(main_dir/'core')]
    # These optional fonts are the only extra LVGL features this App requires.
    # Font declarations do not change LVGL object ABI; rebuild their real data.
    for size in (10,12):
        font=ROOT/f'dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_{size}.c'
        changed.append(font)
        if run/(font.stem+'.o') not in objects:
            objects.append(run/(font.stem+'.o'))
    if changed:
        execute('updated-font', compiler.with_name('gcc.exe'), flags+['-std=c11', '-c']+changed)
    cpp = [main_dir/'apps/bt_keyboard_mouse.cpp', main_dir/'ui/compact_ui.cpp',
           main_dir/'ui/theme.cpp', ROOT/'tooling/firmware/native/ble_mouse_ui_replay.cpp']
    headers = ['apps/bt_keyboard_mouse.hpp','apps/hid_keyboard_model.hpp',
               'core/ble_hid.hpp','core/system_service.hpp','core/ble_connection_flow.hpp',
               'core/ble_peer_store.hpp','core/event_bus.hpp','core/power_mgr.hpp',
               'core/power_policy.hpp','core/theme_model.hpp','ui/app.hpp',
               'ui/compact_ui.hpp','ui/theme.hpp','ui/hid_controls.hpp',
               'ui/control_states.h','ui/text_input.hpp','ui/text_input_layout.hpp']
    paths = cpp+[main_dir/name for name in headers]+[Path(__file__).resolve()]
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
    execute('compile', compiler, flags+['-std=c++17', '-ffunction-sections', '-fdata-sections', '-c']+cpp)
    execute('link', compiler, ['-static-libgcc', '-static-libstdc++', '-Wl,--gc-sections',
            '-o', run/'ui.exe']+objects+[run/(p.stem+'.o') for p in cpp])
    output = execute('run', run/'ui.exe', [])
    after = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
    assert after == before, 'Inputs changed while the UI replay was running'
    result = {'passed': True, 'output': output,
              'production_inputs': after, 'inputs_frozen_during_compile_and_replay': True,
              'lvgl_inputs': inputs, 'cache_manifest_sha256': sha(cache/'c-objects.json'),
              'extra_font_defines': ['LV_FONT_MONTSERRAT_10=1','LV_FONT_MONTSERRAT_12=1'],
              'radio_tested': False, 'device_tested': False, 'flashed': False}
    (run/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
