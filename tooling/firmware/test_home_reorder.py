"""Real HomeScreen + LVGL pointer replay, with isolated navigation/storage adapters."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True)
    ap.add_argument('--c-cache',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();cache=a.c_cache.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve()) and cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    compiler=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    names=['main/ui/'+n for n in ('home_screen.cpp','home_screen.hpp','focus_pager.hpp','compact_ui.cpp','theme.cpp','window_mgr.cpp','window_mgr.hpp')]
    names+=['main/core/app_order_model.hpp','main/core/settings_store.hpp','main/main.cpp','../tooling/firmware/native/home_reorder_replay.cpp','../tooling/firmware/test_home_reorder.py']
    inputs={n:sha(ROOT/n) for n in names}
    for name in ('home_screen','compact_ui'):
        code=(ROOT/f'main/ui/{name}.cpp').read_text(encoding='utf-8')
        (run/(name+'.inc')).write_text(re.sub(r'^#include .*\n','',code,flags=re.M),encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NOT_FOUND=2;\n')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h');manifest=json.loads((cache/'c-objects.json').read_text())
    for n,digest in manifest['inputs'].items():assert sha(cache/n if n=='lv_conf.h' else ROOT/n)==digest,n
    objects=[]
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        if n=='font12.o':continue
        shutil.copyfile(cache/n,run/n);objects.append(run/n)
    def execute(n,args):
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(n+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-7000:])
        return p.stdout.decode(errors='replace').strip()
    def rsp(n,args):
        p=run/(n+'.rsp');p.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args));return '@'+str(p)
    fonts=(10,12,14,24,32,48)
    flags=['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui')]+[f'-DLV_FONT_MONTSERRAT_{size}=1' for size in fonts]
    sources=[ROOT/f'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_{size}.c' for size in fonts]
    sources += [ROOT/f'main/ui/{n}.c' for n in ('lv_font_clock_84','lv_font_almanac_24','lv_font_app_icons')]
    for source in sources:
        obj=run/(source.stem+'.o');execute(source.stem,[str(compiler.with_name('gcc.exe')),rsp(source.stem,flags+['-std=c11','-c',source,'-o',obj])])
        if obj not in objects:objects.append(obj)
    execute('compile',[str(compiler),rsp('compile',flags+['-std=c++20','-Wall','-Wextra','-c',ROOT/'../tooling/firmware/native/home_reorder_replay.cpp',ROOT/'main/ui/theme.cpp'])])
    exe=run/'home-reorder.exe';execute('link',[str(compiler),rsp('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects+[run/'home_reorder_replay.o',run/'theme.o'])])
    result=execute('run',[str(exe)])
    (run/'result.json').write_text(json.dumps(dict(passed=True,result=result,inputs=inputs,device_accessed=False,method=__doc__),indent=2),encoding='utf-8');print(result)
if __name__=='__main__':main()
