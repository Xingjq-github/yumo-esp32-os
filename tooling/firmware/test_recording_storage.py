"""Run production recorder storage code on a fresh synthetic host directory.

VFS calls are redirected to this directory; SD sessions/RTOS are test doubles.
No serial, real SD, firmware build or user recordings are accessed.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess
ROOT = (Path(__file__).resolve().parents[2]/'firmware')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir', type=Path, required=True)
    a = ap.parse_args()
    run = a.run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    inputs = ['main/core/storage_browser.cpp', 'main/core/storage_browser.hpp',
              'main/core/audio_recorder.cpp', 'main/core/audio_recorder.hpp',
              'main/core/sd_media.hpp', 'main/core/wav_format.hpp',
              '../tooling/firmware/native/recording_storage_replay.cpp']
    code = (ROOT / inputs[0]).read_text(encoding='utf-8')
    # Keep complete production enumeration, deletion and worker functions.
    code = code[code.index('bool accepted('):code.index('std::shared_ptr<StorageJob> start(')]
    code = re.sub(r'\bstat\(', 'fake::stat(', code)
    code = code.replace('std::remove(', 'fake::remove(').replace('opendir(', 'fake::opendir(')
    (run / 'storage.inc').write_text(code, encoding='utf-8')
    audio = (ROOT / inputs[2]).read_text(encoding='utf-8')
    audio = audio[audio.index('bool checkpoint('):audio.index('void play(')]
    audio = re.sub(r'\bstat\(', 'fake::stat(', audio)
    audio = audio.replace('::open(', 'fake::open(').replace('gettimeofday(', 'fake::gettimeofday(')
    (run / 'create.inc').write_text(audio, encoding='utf-8')
    cc = Path(__import__('shutil').which('g++') or 'g++')
    env = dict(os.environ)
    env['PATH'] = str(cc.parent) + os.pathsep + env['PATH']
    args = [str(cc), '-std=c++17', '-O1', '-Wall', '-Wextra', '-static-libgcc', '-static-libstdc++',
            '-I'+str(ROOT / 'main'), '-I'+str(run), str(ROOT / inputs[-1]), '-o', str(run / 'storage.exe')]
    p = subprocess.run(args, cwd=run, env=env, capture_output=True, timeout=120,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    (run / 'compile.log').write_bytes(p.stdout+p.stderr)
    if p.returncode: raise RuntimeError(p.stderr.decode(errors='replace'))
    tree = run / 'synthetic-card'
    tree.mkdir()
    p = subprocess.run([str(run/'storage.exe'), str(tree)], cwd=run, env=env, capture_output=True,
                       timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
    (run / 'replay.log').write_bytes(p.stdout+p.stderr)
    if p.returncode: raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
    result = dict(passed=True, result=p.stdout.decode().strip(), real_sd_used=False,
                  inputs={n: hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in inputs},
                  limits='Real production functions, redirected host VFS; SD/RTOS are fakes, not a physical FAT card test.')
    (run/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))

if __name__ == '__main__': main()
