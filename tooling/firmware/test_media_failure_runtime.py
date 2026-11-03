"""Fault-inject production photo/text UI methods without an LVGL rebuild."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True, help='External host g++ compiler path')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--expect-failure', action='store_true')
    args = parser.parse_args()
    run = args.run_dir.resolve()
    assert run.is_relative_to(ROOT / 'artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    source_root = args.source_root.resolve()
    inputs = {}
    for name, cls, methods in (
        ('photo_viewer', 'PhotoViewer', ['scanFiles', 'pollStorage', 'onBack',
         'showList', 'releaseImage', 'releaseSource', 'releaseGif', 'openGif',
         'switchGif', 'pollGif', 'failGif', 'showMessage']),
        ('text_reader', 'TextReader', ['scanFiles', 'pollStorage', 'onBack',
         'showList', 'openFile', 'hidePageNotice', 'showMessage']),
    ):
        path = source_root / f'firmware/main/apps/{name}'
        header_path = path.with_suffix('.hpp')
        source_path = path.with_suffix('.cpp')
        header = re.sub(r'^#include .*\n', '', header_path.read_text('utf-8'), flags=re.M)
        header = header.replace(': public ui::App', '').replace(' override', '').replace('private:', 'public:')
        (run / f'{name}_header.inc').write_text(header, encoding='utf-8')
        source = source_path.read_text('utf-8')
        bodies = []
        for name in methods:
            match = re.search(r'(?:void|bool) ' + cls + '::' + name + r'\(', source)
            if not match and name == 'failGif' and args.expect_failure:
                continue
            assert match, (path, name)
            bodies.append(method(source, match.group(0)))
        (run / f'{path.name}_methods.inc').write_text('namespace apps {\n' + '\n'.join(bodies) + '\n}', encoding='utf-8')
        for file in (header_path, source_path):
            inputs[file.relative_to(source_root).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
    compiler = args.compiler.resolve()
    test = ROOT / 'tooling/firmware/native/media_failure_replay.cpp'
    exe = run / 'media-failure.exe'
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    command = [str(compiler), '-std=c++17', '-O1', '-Wall', '-Wextra', '-static',
               '-I' + str(run), '-I' + str(ROOT / 'firmware/main'),
               '-I' + str(ROOT / 'firmware/components/photo_jpeg/include'), str(test), '-o', str(exe)]
    compiled = subprocess.run(command, env=env, capture_output=True, timeout=60,
                              creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    assert compiled.returncode == 0, compiled.stderr.decode(errors='replace')
    results = {}
    for scenario in ('gif', 'photo-list', 'text-list', 'text-open'):
        proc = subprocess.run([str(exe), scenario], cwd=run, env=env,
                              capture_output=True, timeout=10, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run / (scenario + '.log')).write_bytes(proc.stdout + proc.stderr)
        results[scenario] = {'returncode': proc.returncode, 'stdout': proc.stdout.decode(errors='replace'),
                             'stderr': proc.stderr.decode(errors='replace')}
        assert (proc.returncode != 0) == args.expect_failure, results[scenario]
    for file in (Path(__file__), test, ROOT / 'firmware/main/core/gif_player.hpp',
                 ROOT / 'firmware/main/core/storage_browser.hpp'):
        inputs[file.relative_to(ROOT).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
    result = {'passed': True, 'expected_baseline_failure': args.expect_failure, 'scenarios': results,
              'inputs': inputs, 'device_accessed': False, 'real_user_data_accessed': False,
              'method': 'Production PhotoViewer/TextReader methods and GIF mailbox; synthetic widgets/timers, '
                        'allocation failures, task completions and key sessions. No full LVGL/firmware build.'}
    (run / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
