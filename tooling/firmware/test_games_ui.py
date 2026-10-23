"""Exercise the real Photo/FileManager Apps and LVGL UI with fake hardware."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path,required=True)
    a=ap.parse_args();run=a.run_dir.resolve();cache=a.c_cache.resolve();compiler=a.compiler.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve()) and cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False);env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    names=['main/apps/'+n for n in ('dino.cpp','dino.hpp','dino_model.hpp','dino_sprites.hpp','gravity_maze.cpp','gravity_maze.hpp','maze_model.hpp','maze_visual.hpp','maze_fog.hpp')]
    names+=['main/ui/compact_ui.cpp','main/ui/theme.cpp','main/ui/game_widgets.h','../tooling/firmware/native/games_ui_replay.cpp','../tooling/firmware/test_games_ui.py']
    inputs={n:sha(ROOT/n) for n in names}
    for folder,name in [('apps','dino'),('apps','gravity_maze'),('ui','compact_ui')]:
        code=(ROOT/f'main/{folder}/{name}.cpp').read_text(encoding='utf-8')
        (run/(name+'.inc')).write_text(re.sub(r'^#include .*\n','',code,flags=re.M),encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    (run/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_TIMEOUT 0x107\n')
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h');manifest=json.loads((cache/'c-objects.json').read_text())
    changed=[];objects=[];updated=dict(manifest['inputs'])
    for n,digest in manifest['inputs'].items():
        p=cache/n if n=='lv_conf.h' else ROOT/n
        if sha(p)!=digest:
            assert p==ROOT/'main/ui/lv_font_menu_14.c','Unexpected cache mismatch: '+n
            changed.append(p);updated[n]=sha(p)
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        shutil.copyfile(cache/n,run/n);objects.append(run/n)
    def execute(n,args):
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(n+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-7000:])
        return p.stdout.decode(errors='replace').strip()
    def rsp(n,args):
        p=run/(n+'.rsp');p.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args));return '@'+str(p)
    flags=['-O1','-DLV_FONT_MONTSERRAT_12=1','-DLV_FONT_MONTSERRAT_10=1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/core')]
    if changed:execute('updated-font',[str(compiler.with_name('gcc.exe')),rsp('updated-font',flags+['-std=c11','-c']+changed)])
    font=ROOT/'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_12.c'
    execute('font12',[str(compiler.with_name('gcc.exe')),rsp('font12',flags+['-std=c11','-c',font,'-o',run/'font12.o'])])
    if run/'font12.o' not in objects:objects.append(run/'font12.o')
    (run/'c-objects.json').write_text(json.dumps({'inputs':updated,'objects':{p.name:sha(p) for p in objects}}))
    for name in ('lv_font_clock_84','lv_font_almanac_24'):
        execute(name,[str(compiler.with_name('gcc.exe')),rsp(name,flags+['-std=c11','-c',ROOT/f'main/ui/{name}.c','-o',run/(name+'.o')])]);objects.append(run/(name+'.o'))
    execute('compile',[str(compiler),rsp('compile',flags+['-std=c++20','-c',ROOT/'../tooling/firmware/native/games_ui_replay.cpp',ROOT/'main/ui/theme.cpp'])])
    exe=run/'games-ui.exe';execute('link',[str(compiler),rsp('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects+[run/'games_ui_replay.o',run/'theme.o'])])
    output=execute('run',[str(exe)])
    result=dict(passed=True,ui=output,inputs=inputs,synthetic_only=True,device_accessed=False,
        method='Production Dino/GravityMaze Apps, models, fog, visuals and compact widgets on real LVGL; sensor/audio/PSRAM/power adapters are controlled doubles')
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
