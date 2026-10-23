"""Replay real FC save widgets with local LVGL and synthetic emulator state."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path,required=True)
    a=ap.parse_args();run=a.run_dir.resolve();cache=a.c_cache.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve()) and cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++');env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    src=(ROOT/'main/apps/fc_player_saves.cpp').read_text(encoding='utf-8')
    (run/'fc_player_saves.inc').write_text(re.sub(r'^#include .*\n','',src,flags=re.M),encoding='utf-8')
    compact=(ROOT/'main/ui/compact_ui.cpp').read_text(encoding='utf-8')
    (run/'compact_ui.inc').write_text(re.sub(r'^#include .*\n','',compact,flags=re.M),encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 259\n#define ESP_ERR_NOT_FOUND 261\n#define ESP_ERR_TIMEOUT 263\n')
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h')
    manifest=json.loads((cache/'c-objects.json').read_text());objects=[]
    for n,digest in manifest['inputs'].items():assert sha(cache/n if n=='lv_conf.h' else ROOT/n)==digest,n
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        shutil.copyfile(cache/n,run/n);objects.append(run/n)
    def execute(name,args):
        rsp=run/(name+'.rsp');rsp.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),encoding='utf-8')
        p=subprocess.run([str(cc),'@'+str(rsp)],cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode(errors='replace')[-6000:]
    flags=['-std=c++17','-O1','-DLV_FONT_MONTSERRAT_10=1','-DLV_FONT_MONTSERRAT_12=1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui')]
    execute('compile',flags+['-c',ROOT/'../tooling/firmware/native/fc_saves_ui_replay.cpp',ROOT/'main/ui/theme.cpp'])
    exe=run/'fc-saves-ui.exe';execute('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects+[run/'fc_saves_ui_replay.o',run/'theme.o'])
    p=subprocess.run([str(exe)],cwd=run,env=env,capture_output=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
    (run/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode(errors='replace')
    result=dict(passed=True,output=p.stdout.decode().strip(),source_sha256=sha(ROOT/'main/apps/fc_player_saves.cpp'),device_accessed=False)
    (run/'result.json').write_text(json.dumps(result,indent=2));print(p.stdout.decode().strip())
if __name__=='__main__':main()
