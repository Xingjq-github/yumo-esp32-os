"""Replay production WindowManager and VaultPattern over a patterned home.

Uses existing local LVGL C objects; no device or firmware build access.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--run-dir', type=Path, required=True)
    p.add_argument('--lvgl-root', type=Path, required=True, help='External pinned LVGL source directory')
    p.add_argument('--c-cache', type=Path, required=True)
    a = p.parse_args()
    run, cache = a.run_dir.resolve(), a.c_cache.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve())
    assert cache.is_relative_to((ROOT/'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(cache/'lv_conf.h', run/'lv_conf.h')
    hashes = {}
    for name in ('window_mgr', 'vault_pattern'):
        source = ROOT/'firmware/main/ui'/(name+'.cpp')
        hashes[str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
        # Same production definitions with only includes moved into the harness
        # to substitute hardware/desktop adapters without editing firmware.
        code = re.sub(r'^#include .*\n', '', source.read_text(encoding='utf8'), flags=re.M)
        (run/(name+'.inc')).write_text(code, encoding='utf8')
    lv = a.lvgl_root.resolve()
    objects = []
    files = [x for x in (lv/'src').rglob('*.c') if not set(x.relative_to(lv/'src').parts)&{'libs','drivers'}]
    files += list((lv/'src/libs/bin_decoder').glob('*.c'))
    for source in files:
        obj = cache/(source.stem+'.o')
        assert obj.is_file() and obj.stat().st_mtime_ns >= source.stat().st_mtime_ns
        objects.append(obj)
    source = ROOT/'tooling/firmware/native/app_background_replay.cpp'
    hashes[str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    exe = run/'app_background.exe'
    args = ['-O1','-std=c++17','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(lv),'-I'+str(ROOT/'firmware/main'),'-o',str(exe),str(source)]+list(map(str,objects))
    rsp = run/'compile.rsp'
    rsp.write_text('\n'.join('"'+v.replace('\\','/')+'"' for v in args))
    env = dict(os.environ);env['PATH'] = str(a.compiler.resolve().parent)+os.pathsep+env.get('PATH','')
    for stage,cmd in [('compile',[str(a.compiler.resolve()),'@'+str(rsp)]),('execute',[str(exe)])]:
        result = subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=100)
        (run/(stage+'.stdout.log')).write_bytes(result.stdout)
        (run/(stage+'.stderr.log')).write_bytes(result.stderr)
        if result.returncode:
            print(result.stderr.decode(errors='replace')[-10000:]);raise RuntimeError(stage+' failed')
    output = json.loads(result.stdout.decode().strip().splitlines()[-1])
    (run/'result.json').write_text(json.dumps(dict(passed=True,checks=output,inputs=hashes,
        method='Production WindowManager and VaultPattern; real LVGL; synthetic high-contrast home; compare app frames with independent-screen reference.20-row DMA-like byte-swapped double buffer. Hardware/status/compact helpers stubbed.',
        firmware_built=False,device_accessed=False),indent=2))
    print(json.dumps(output))

if __name__ == '__main__': main()
