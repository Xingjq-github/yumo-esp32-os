"""Compare production status/radio and PK rendering with the pre-change source.

Reuses pinned local LVGL objects; does not build firmware or access a device.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    begin = text.index('{', start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--c-cache', type=Path, required=True)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--lvgl-root', type=Path, required=True, help='External pinned LVGL source directory')
    a = parser.parse_args()
    run, cache = a.run_dir.resolve(), a.c_cache.resolve()
    assert run.is_relative_to(ROOT / 'artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    lv = a.lvgl_root.resolve()
    files = [p for p in (lv / 'src').rglob('*.c') if not set(p.relative_to(lv / 'src').parts) & {'libs', 'drivers'}]
    files += list((lv / 'src/libs/bin_decoder').glob('*.c'))
    objects = [cache / (p.stem + '.o') for p in files]
    for source, obj in zip(files, objects):
        assert obj.is_file() and obj.stat().st_mtime_ns >= source.stat().st_mtime_ns
    env = dict(os.environ)
    env['PATH'] = str(a.compiler.resolve().parent) + os.pathsep + env.get('PATH', '')
    reports, inputs = {}, {}
    for variant, base in [('before', a.before.resolve()), ('after', ROOT)]:
        folder = run / variant
        folder.mkdir()
        shutil.copyfile(cache / 'lv_conf.h', folder / 'lv_conf.h')
        contents = {}
        for name in ['ui/window_mgr', 'ui/quick_panel', 'apps/randomizer']:
            path = base / 'firmware/main' / (name + '.cpp')
            contents[name] = path.read_text(encoding='utf8')
            inputs[variant + '/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
        status = contents['ui/window_mgr']
        status = status[status.index('namespace {') + len('namespace {'):status.index('\nWindowManager &')]
        status = status[:status.rfind('}')]
        (folder / 'status.inc').write_text(status, encoding='utf8')
        (folder / 'panel.inc').write_text(function(contents['ui/quick_panel'], 'void refresh()'), encoding='utf8')
        random = contents['apps/randomizer']
        (folder / 'random.inc').write_text('\n'.join(function(random, 'void Randomizer::' + name) for name in ['paintPk()', 'tickPk()']), encoding='utf8')
        exe = folder / 'replay.exe'
        args = ['-O1', '-std=c++17', '-DLV_CONF_INCLUDE_SIMPLE', '-I' + str(folder), '-I' + str(lv), '-I' + str(ROOT / 'firmware/main'), '-o', str(exe), str(ROOT / 'tooling/firmware/native/ui_idle_replay.cpp')] + list(map(str, objects))
        if variant == 'after':
            args.insert(0, '-DEXPECT_IDLE=1')
        rsp = folder / 'compile.rsp'
        rsp.write_text('\n'.join('"' + arg.replace('\\', '/') + '"' for arg in args))
        for stage, command in [('compile', [str(a.compiler.resolve()), '@' + str(rsp)]), ('execute', [str(exe)])]:
            result = subprocess.run(command, cwd=folder, env=env, capture_output=True, timeout=100)
            (folder / (stage + '.stdout.log')).write_bytes(result.stdout)
            (folder / (stage + '.stderr.log')).write_bytes(result.stderr)
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors='replace')[-12000:])
        reports[variant] = json.loads(result.stdout.decode().strip().splitlines()[-1])
    for key in ['pixel_digest', 'state_digest', 'pk_settlements']:
        assert reports['before'][key] == reports['after'][key], (key, reports)
    assert reports['before']['idle_flushes'] > 0 and reports['after']['idle_flushes'] == 0
    receipt = dict(passed=True, reports=reports, inputs=inputs,
        method='Byte-exact extracted production status update, quick-panel refresh, PK paint/tick; real pinned LVGL with 20-row double buffers. Hardware snapshots and fixture creation are synthetic; compare pre-change pixel/state digests.',
        firmware_built=False, device_accessed=False)
    (run / 'result.json').write_text(json.dumps(receipt, indent=2), encoding='utf8')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
