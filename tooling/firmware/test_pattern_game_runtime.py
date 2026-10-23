"""Run production learning UI, parser and SD worker with local LVGL and fake hardware."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path,required=True)
    a=ap.parse_args();run=a.run_dir.resolve();cache=a.c_cache.resolve();cc=a.compiler.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve()) and cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False);env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    sources=['main/apps/design_patterns.cpp','main/apps/design_patterns.hpp','main/core/pattern_format.cpp',
        'main/core/pattern_format.hpp','main/core/pattern_play.hpp','main/core/pattern_store.cpp','main/core/pattern_store.hpp','main/core/pattern_data.cpp',
        'main/ui/theme.cpp','main/ui/compact_ui.cpp','main/ui/lv_font_menu_14.c','../tooling/firmware/native/pattern_game_replay.cpp']
    inputs={n:sha(ROOT/n) for n in sources}
    for n,target in [('main/apps/design_patterns.cpp','patterns.inc'),('main/core/pattern_store.cpp','store.inc')]:
        (run/target).write_text(re.sub(r'^#include .*\n','',(ROOT/n).read_text(encoding='utf-8'),flags=re.M),encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h');manifest=json.loads((cache/'c-objects.json').read_text())
    changed=[];objects=[]
    for n,digest in manifest['inputs'].items():
        p=cache/n if n=='lv_conf.h' else ROOT/n
        if sha(p)!=digest:
            assert p==ROOT/'main/ui/lv_font_menu_14.c','Unexpected cache mismatch: '+n
            changed.append(p)
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        shutil.copyfile(cache/n,run/n);objects.append(run/n)
    def execute(n,args):
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(n+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-8000:])
        return p.stdout.decode(errors='replace').strip()
    def rsp(n,args):
        p=run/(n+'.rsp');p.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args));return '@'+str(p)
    flags=['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),
        '-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'../dependencies/managed_components/espressif__cjson/cJSON')]
    extra=[ROOT/'main/ui/lv_font_code_13.c',ROOT/'../dependencies/managed_components/espressif__cjson/cJSON/cJSON.c']
    execute('c',[str(cc.with_name('gcc.exe')),rsp('c',flags+['-std=c11','-c']+changed+extra)])
    objects += [run/(p.stem+'.o') for p in extra]
    cpp=[ROOT/n for n in ['../tooling/firmware/native/pattern_game_replay.cpp','main/ui/theme.cpp','main/ui/compact_ui.cpp','main/core/pattern_format.cpp','main/core/pattern_data.cpp']]
    execute('compile',[str(cc),rsp('compile',flags+['-std=c++17','-c']+cpp)])
    objects += [run/(p.stem+'.o') for p in cpp]
    exe=run/'patterns.exe';execute('link',[str(cc),rsp('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects)])
    output=execute('run',[str(exe)])
    result=dict(passed=True,output=output,inputs=inputs,device_accessed=False,real_user_data_accessed=False,
        method='Production App, SD worker, format, package, theme and LVGL. Deterministic RTOS, SD admission, power and navigation doubles; isolated filesystem.')
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
