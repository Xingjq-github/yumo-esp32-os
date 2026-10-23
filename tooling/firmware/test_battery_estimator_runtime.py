"""Compile/run the production battery estimator with host time-series inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, help='Host g++ path; defaults to PATH')
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--baseline-header', type=Path)
    args = parser.parse_args()
    run = args.run_dir.resolve()
    if not run.is_relative_to((ROOT / 'artifacts/validation').resolve()):
        raise SystemExit('Run directory must be inside this project artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    selected = args.compiler or shutil.which('g++')
    if not selected:
        raise SystemExit('Install host g++ on PATH or pass --compiler')
    compiler = Path(selected).resolve()
    if not compiler.is_file():
        raise SystemExit('Host compiler not found: ' + str(compiler))
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    source = ROOT / 'tooling/firmware/native/battery_estimator_replay.cpp'
    estimator = ROOT / 'firmware/main/core/battery_estimator.hpp'
    command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I' + str(ROOT / 'firmware/main'), '-I' + str(run),
               str(source), '-o', str(run / 'battery.exe'), '-static-libgcc', '-static-libstdc++']
    inputs = [source, estimator, Path(__file__)]
    if args.baseline_header:
        old = args.baseline_header.read_text(encoding='utf-8')
        if old.count('namespace sys {') != 1:
            raise SystemExit('Unexpected baseline header namespace')
        (run / 'battery_previous.hpp').write_text(old.replace('namespace sys {', 'namespace previous_sys {', 1), encoding='utf-8')
        command.append('-DBATTERY_PREVIOUS')
        inputs.append(args.baseline_header)
    outputs = {}
    for name, argv in [('compile', command), ('replay', [str(run / 'battery.exe')])]:
        result = subprocess.run(argv, cwd=run, env=env, capture_output=True, timeout=90,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        (run / (name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise SystemExit((result.stdout + result.stderr).decode('utf-8', errors='replace'))
        outputs[name] = result.stdout.decode('utf-8')
    result = dict(passed=True, method='actual production C++ estimator, simulated voltage/time',
                  hardware_accessed=False, capacity_calibrated=False, output=outputs['replay'],
                  inputs={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    (run / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(outputs['replay'], end='')


if __name__ == '__main__':
    main()
