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
    inputs={n:sha(ROOT/n) for n in ('main/apps/file_manager.cpp','main/apps/file_manager.hpp','main/apps/photo_viewer.cpp','main/apps/photo_viewer.hpp','main/apps/text_reader.cpp','main/apps/text_reader.hpp','main/ui/virtual_file_list.hpp','main/core/gif_player.hpp','main/core/file_manager.hpp','main/core/file_operations.cpp','main/ui/compact_ui.cpp','main/ui/theme.cpp','../tooling/firmware/native/files_photo_ui_replay.cpp','main/ui/lv_font_menu_14.c')}
    for folder,name in [('apps','file_manager'),('apps','photo_viewer'),('apps','text_reader'),('ui','compact_ui')]:
        code=(ROOT/f'main/{folder}/{name}.cpp').read_text(encoding='utf-8')
        (run/(name+'.inc')).write_text(re.sub(r'^#include .*\n','',code,flags=re.M),encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
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
    flags=['-O1','-DLV_FONT_MONTSERRAT_12=1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/core')]
    if changed:execute('updated-font',[str(compiler.with_name('gcc.exe')),rsp('updated-font',flags+['-std=c11','-c']+changed)])
    font=ROOT/'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_12.c'
    execute('font12',[str(compiler.with_name('gcc.exe')),rsp('font12',flags+['-std=c11','-c',font,'-o',run/'font12.o'])])
    if run/'font12.o' not in objects:objects.append(run/'font12.o')
    (run/'c-objects.json').write_text(json.dumps({'inputs':updated,'objects':{p.name:sha(p) for p in objects}}))
    execute('compile',[str(compiler),rsp('compile',flags+['-std=c++17','-c',ROOT/'../tooling/firmware/native/files_photo_ui_replay.cpp',ROOT/'main/ui/theme.cpp',ROOT/'main/core/file_operations.cpp'])])
    exe=run/'files-photo-ui.exe';execute('link',[str(compiler),rsp('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects+[run/'files_photo_ui_replay.o',run/'theme.o',run/'file_operations.o'])])
    output=execute('run',[str(exe)])
    result=dict(passed=True,ui=output,inputs=inputs,synthetic_only=True,device_accessed=False,
        method='Production PhotoViewer/FileManager Apps and compact widgets on local LVGL; hardware and asynchronous jobs are test doubles; separate native engine/decoder tests use actual code')
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
