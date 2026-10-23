"""Replay the unchanged quick-panel implementation using local LVGL and fake services."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path);a=ap.parse_args()
    a.compiler=a.compiler.resolve()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    lv=ROOT/'../dependencies/managed_components/lvgl__lvgl'
    source=(ROOT/'main/ui/quick_panel.cpp').read_text(encoding='utf8')
    (run/'quick_panel.inc').write_text(re.sub(r'^#include .*\n','',source,flags=re.M),encoding='utf8')
    compact=(ROOT/'main/ui/compact_ui.cpp').read_text(encoding='utf8')
    compact=compact[compact.index('namespace ui::compact {'):compact.index('lv_obj_t *row(')]+compact[compact.index('void setText('):]
    (run/'compact.inc').write_text(compact,encoding='utf8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 259\n#define ESP_ERR_NOT_FOUND 261\n#define ESP_ERR_TIMEOUT 263\n')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    (run/'esp_wifi_types.h').write_text('#pragma once\nstruct wifi_ap_record_t {};\n')
    (run/'lv_conf.h').write_text('''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (4*1024*1024U)
#define LV_USE_OS LV_OS_NONE
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_ASSERT_OBJ 1
#define LV_USE_ASSERT_STYLE 1
#define LV_ASSERT_HANDLER {abort();}
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#endif
''')
    common=['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(lv),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui')]
    folders=('core','display','draw','image','fs','logging','indev','layouts','misc','osal','stdlib','themes','tick','widgets','libs/bin_decoder')
    cfiles=[lv/'src/lv_init.c']+[p for f in folders for p in (lv/'src'/f).rglob('*.c')]
    cfiles += list((lv/'src/font').rglob('*.c'))
    cfiles += [ROOT/'main/ui'/f for f in ('lv_font_cjk_16.c','lv_font_menu_14.c','lv_font_reading_15.c')]
    env=dict(os.environ);env['PATH']=str(a.compiler.resolve().parent)+os.pathsep+env.get('PATH','')
    def execute(name,args):
        result=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=240)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-8000:])
        return result.stdout.decode(errors='replace')
    def rsp(name,args):
        path=run/(name+'.rsp');path.write_text('\n'.join('"'+str(x).replace('\\','/')+'"' for x in args),encoding='utf8');return '@'+str(path)
    fingerprints={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in cfiles}
    fingerprints['lv_conf.h']=hashlib.sha256((run/'lv_conf.h').read_bytes()).hexdigest()
    if a.c_cache:
        cache=a.c_cache.resolve();assert cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
        manifest=json.loads((cache/'c-objects.json').read_text())
        changed=[]
        for n,sha in manifest['inputs'].items():
            if fingerprints.get(n)!=sha:
                p=ROOT/n
                assert p in (ROOT/'main/ui/lv_font_menu_14.c',ROOT/'main/ui/lv_font_cjk_16.c'),'LVGL or host configuration changed; rebuild C objects'
                changed.append(p)
        for name,sha in manifest['objects'].items():
            obj=cache/name;assert hashlib.sha256(obj.read_bytes()).hexdigest()==sha
            shutil.copyfile(obj,run/name)
        added=[p for p in cfiles if str(p.relative_to(ROOT)) not in manifest['inputs']]
        if added:execute('c-compile-added',[str(a.compiler.with_name('gcc.exe')),rsp('c-compile-added',common+['-c']+added)])
        if changed:execute('c-compile-updated-font',[str(a.compiler.with_name('gcc.exe')),rsp('c-compile-updated-font',common+['-c']+changed)])
    else:execute('c-compile',[str(a.compiler.with_name('gcc.exe')),rsp('c-compile',common+['-c']+cfiles)])
    objects={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in run.glob('*.o')}
    (run/'c-objects.json').write_text(json.dumps({'inputs':fingerprints,'objects':objects}))
    cpp=[ROOT/'../tooling/firmware/native/quick_panel_replay.cpp',ROOT/'main/ui/theme.cpp']
    execute('cpp-compile',[str(a.compiler),rsp('cpp-compile',common+['-std=c++17','-c']+cpp)])
    exe=run/'quick_panel.exe'
    execute('link',[str(a.compiler),rsp('link',['-static-libgcc','-static-libstdc++','-o',exe]+list(run.glob('*.o')))])
    output=execute('execute',[str(exe)])
    receipt={'passed':True,'result':output.strip(),'method':'production panel/compact/theme code against real local LVGL; injected hardware snapshots; no device access',
             'source_sha256':hashlib.sha256(source.encode()).hexdigest()}
    (run/'result.json').write_text(json.dumps(receipt,indent=2),encoding='utf8');print(output.strip())
if __name__=='__main__':main()
