"""Real LVGL/App playback UI; worker and system settings are test doubles."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cache=a.c_cache.resolve()
    assert cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve()
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    code=(ROOT/'main/apps/mp3_music.cpp').read_text(encoding='utf-8')
    (run/'app.inc').write_text(re.sub(r'^#include .*\n','',code,flags=re.M),encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h');manifest=json.loads((cache/'c-objects.json').read_text())
    objects=[];changed=[]
    for n,digest in manifest['inputs'].items():
        p=cache/n if n=='lv_conf.h' else ROOT/n
        if sha(p)!=digest:
            assert p in (ROOT/'main/ui/lv_font_menu_14.c',ROOT/'main/ui/lv_font_app_icons.c'),n
            changed.append(p)
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        shutil.copyfile(cache/n,run/n);objects.append(run/n)
    def execute(name,args):
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=180,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-8000:])
        return p.stdout.decode('utf-8')
    def rsp(name,args):
        p=run/(name+'.rsp');p.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args));return '@'+str(p)
    flags=['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui')]
    icons=ROOT/'main/ui/lv_font_app_icons.c'
    if 'main/ui/lv_font_app_icons.c' not in manifest['inputs']:
        changed.append(icons);objects.append(run/'lv_font_app_icons.o')
    changed.append(ROOT/'main/ui/lv_font_music_22.c');objects.append(run/'lv_font_music_22.o')
    if changed:execute('updated-font',[str(Path(__import__('shutil').which('gcc') or 'gcc').resolve()),rsp('updated-font',flags+['-std=c11','-c']+changed)])
    execute('compile',[str(cc),rsp('compile',flags+['-std=c++17','-c',ROOT/'../tooling/firmware/native/mp3_music_ui_test.cpp',ROOT/'main/ui/theme.cpp',ROOT/'main/ui/compact_ui.cpp','-ffunction-sections','-fdata-sections'])])
    execute('link',[str(cc),rsp('link',['-static-libgcc','-static-libstdc++','-Wl,--gc-sections','-o',run/'ui.exe']+objects+[run/'mp3_music_ui_test.o',run/'theme.o',run/'compact_ui.o'])])
    output=execute('run',[str(run/'ui.exe')])
    files=['main/apps/mp3_music.cpp','main/apps/mp3_music.hpp','main/ui/lv_font_music_22.c','main/ui/compact_ui.cpp','main/ui/theme.cpp','../tooling/firmware/native/mp3_music_ui_test.cpp']
    (run/'result.json').write_text(json.dumps(dict(passed=True,output=output,inputs={n:sha(ROOT/n) for n in files},physical_touch_tested=False),ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
