"""Full production screen-protection UI with real dual-worker LVGL and pointer dispatch.

Board power/input and wall clock are deterministic stubs. No firmware/device access.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--lvgl-root', type=Path, required=True, help='External pinned LVGL source directory')
    parser.add_argument('--compiler', type=Path, required=True, help='External host g++ compiler path')
    parser.add_argument('--c-cache', type=Path, required=True)
    args = parser.parse_args()
    run, cache = args.run_dir.resolve(), args.c_cache.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    lv = args.lvgl_root.resolve()
    sources = [p for p in (lv/'src').rglob('*.c') if not set(p.relative_to(lv/'src').parts)&{'libs','drivers'}]
    sources += list((lv/'src/libs/bin_decoder').glob('*.c'))
    font = lv/'src/font/lv_font_montserrat_24.c'
    objects = []
    for source in sources:
        if source == font:
            continue
        obj = cache/(source.stem+'.o')
        assert obj.is_file() and obj.stat().st_mtime_ns >= source.stat().st_mtime_ns, source
        objects.append(obj)
    original_conf = (cache/'lv_conf.h').read_text(encoding='utf8')
    # This cache disabled the 24px font. Enabling this one const font changes no
    # LVGL structure/ABI; rebuild only its previously empty translation unit.
    assert '#define LV_FONT_MONTSERRAT_24 0' in original_conf
    (run/'lv_conf.h').write_text(original_conf.replace('#define LV_FONT_MONTSERRAT_24 0','#define LV_FONT_MONTSERRAT_24 1'),encoding='utf8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n',encoding='utf8')
    (run/'clock_stub.hpp').write_text('''#pragma once
#include <ctime>
time_t replay_time(time_t *);
tm *replay_localtime(const time_t *,tm *);
#define time replay_time
#define localtime_r replay_localtime
''',encoding='utf8')
    cc = args.compiler.resolve()
    gcc = cc.with_name('gcc.exe')
    env = dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    flags = ['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(lv),'-I'+str(ROOT/'firmware/main')]
    def execute(stage, command):
        try:
            result = subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=60,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        except subprocess.TimeoutExpired as error:
            (run/(stage+'.log')).write_bytes((error.stdout or b'')+(error.stderr or b''))
            raise
        (run/(stage+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-16000:])
        return result
    execute('font',[str(gcc),*flags,'-std=c11','-c',str(font),'-o',str(run/'font24.o')])
    model = ROOT/'tooling/firmware/screen_protection_model_test.cpp'
    execute('model',[str(cc),*flags,'-std=c++17','-fsyntax-only',str(model)])
    production = ROOT/'firmware/main/ui/screen_protection.cpp'
    native = ROOT/'tooling/firmware/native/screen_protection_replay.cpp'
    compile_args = [*flags,'-std=c++17','-include',str(run/'clock_stub.hpp'),str(production),str(native),
                    *map(str,objects),str(run/'font24.o'),'-o',str(run/'replay.exe')]
    (run/'compile.rsp').write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in compile_args),encoding='utf8')
    execute('compile',[str(cc),'@'+str(run/'compile.rsp')])
    result=execute('replay',[str(run/'replay.exe')])
    outputs=json.loads(result.stdout.decode().strip().splitlines()[-1])
    inputs=[production,native,model,Path(__file__).resolve(),ROOT/'firmware/main/ui/screen_protection.hpp',ROOT/'firmware/main/ui/screen_protection_model.hpp',cache/'lv_conf.h',font]
    report=dict(passed=True,outputs=outputs,inputs={(p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else '@lvgl/'+p.relative_to(lv).as_posix() if p.is_relative_to(lv) else '@cache/'+p.relative_to(cache).as_posix()):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                method=__doc__,cached_objects=len(objects),font_only_config_override=True,device_accessed=False,firmware_built=False)
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(report))


if __name__=='__main__':main()
