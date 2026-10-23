"""Replay production recording playback and LVGL controls with synthetic data.

The native Sonic ABI is a streaming test double, not Espressif's binary DSP.
No firmware/serial/SD access. SDK sound quality still requires device listening.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path,required=True)
    a=ap.parse_args();run=a.run_dir.resolve();cache=a.c_cache.resolve();compiler=a.compiler.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve()) and cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False);env=dict(os.environ)
    env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    inputs={}
    for n in ('main/core/audio_recorder.cpp','main/core/audio_recorder.hpp','main/core/recorder_transport.hpp',
              'main/core/recorder_playback.hpp','main/core/storage_browser.hpp','main/apps/recorder.cpp','main/apps/recorder.hpp',
              'main/ui/recorder_layout.hpp','main/ui/compact_ui.cpp','main/ui/control_states.h','main/ui/theme.cpp',
              '../tooling/firmware/native/recorder_playback_replay.cpp','../tooling/firmware/native/recorder_ui_replay.cpp'):
        inputs[n]=sha(ROOT/n)
    audio=(ROOT/'main/core/audio_recorder.cpp').read_text(encoding='utf-8')
    (run/'audio_play.inc').write_text('void play('+audio.split('void play(',1)[1].split('void execute(',1)[0],encoding='utf-8')
    for name,path in (('compact_ui','main/ui/compact_ui.cpp'),('recorder','main/apps/recorder.cpp')):
        code=re.sub(r'^#include .*\n','',(ROOT/path).read_text(encoding='utf-8'),flags=re.M)
        (run/(name+'.inc')).write_text(code,encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    def execute(name,args):
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-7000:])
        return p.stdout.decode(errors='replace').strip()
    def rsp(name,args):
        path=run/(name+'.rsp');path.write_text('\n'.join('"'+str(x).replace('\\','/')+'"' for x in args))
        return '@'+str(path)
    common=['-O1','-std=c++17','-I'+str(run),'-I'+str(ROOT/'main'),'-static-libgcc','-static-libstdc++']
    native=run/'playback.exe'
    execute('compile-playback',[str(compiler)]+common+['-I'+str(ROOT/'../dependencies/managed_components/espressif__esp_audio_effects/include'),
            str(ROOT/'../tooling/firmware/native/recorder_playback_replay.cpp'),'-o',str(native)])
    playback=execute('playback',[str(native)])
    manifest=json.loads((cache/'c-objects.json').read_text())
    changed=[]
    for name,digest in manifest['inputs'].items():
        source=cache/name if name=='lv_conf.h' else ROOT/name
        if sha(source)!=digest:
            assert source==ROOT/'main/ui/lv_font_menu_14.c',str(source)
            changed.append(source)
    objects=[]
    for name,digest in manifest['objects'].items():
        assert sha(cache/name)==digest,name
        shutil.copyfile(cache/name,run/name);objects.append(run/name)
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h')
    for source in changed:
        inputs[source.relative_to(ROOT).as_posix()]=sha(source)
        execute('compile-menu-font',[str(compiler.parent/'gcc.exe'),'-O1','-std=c11',
                '-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),
                '-c',str(source),'-o',str(run/(source.stem+'.o'))])
    # This App also uses Montserrat 12; the shared cache did not need it.
    # This font-only option changes no LVGL ABI or cached core object layout.
    font=ROOT/'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_12.c'
    inputs[font.relative_to(ROOT).as_posix()]=sha(font)
    execute('compile-font12',[str(compiler.parent/'gcc.exe'),'-O1','-std=c11','-DLV_CONF_INCLUDE_SIMPLE',
            '-DLV_FONT_MONTSERRAT_12=1','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),
            '-c',str(font),'-o',str(run/'font12.o')])
    objects.append(run/'font12.o')
    flags=common+['-DLV_CONF_INCLUDE_SIMPLE','-DLV_FONT_MONTSERRAT_12=1','-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main/ui')]
    execute('compile-ui',[str(compiler),rsp('compile-ui',flags+['-c',ROOT/'../tooling/firmware/native/recorder_ui_replay.cpp',ROOT/'main/ui/theme.cpp'])])
    ui=run/'recorder-ui.exe'
    execute('link-ui',[str(compiler),rsp('link-ui',['-static-libgcc','-static-libstdc++','-o',ui]+objects+[run/'recorder_ui_replay.o',run/'theme.o'])])
    state=execute('ui',[str(ui)])
    result=dict(passed=True,playback=playback,ui=state,inputs=inputs,synthetic_only=True,
                sonic='SDK ABI test double; vendor binary pitch not measured on host',device_accessed=False)
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
