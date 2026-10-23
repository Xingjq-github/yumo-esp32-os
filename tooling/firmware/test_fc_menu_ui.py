"""Replay real FC option widgets with local LVGL and synthetic emulator state."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--c-cache',type=Path,required=True)
    a=ap.parse_args();run=a.run_dir.resolve();cache=a.c_cache.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve()) and cache.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    src=(ROOT/'main/apps/fc_player.cpp').read_text(encoding='utf-8')
    bluetooth=src[src.index('void FcPlayer::bluetoothNotice('):src.index('void FcPlayer::refresh()')]
    (run/'fc_bluetooth.inc').write_text('namespace apps {\n'+bluetooth+'\n}\n',encoding='utf-8')
    section=src[src.index('void FcPlayer::createMenu()'):src.index('void FcPlayer::createControls()')]
    (run/'fc_menu.inc').write_text('namespace apps {\n'+section+'\n}\n',encoding='utf-8')
    pause=src[src.index('void FcPlayer::showMenu()'):src.index('void FcPlayer::stopGame(')]
    (run/'fc_pause.inc').write_text('namespace apps {\n'+pause+'\n}\n',encoding='utf-8')
    controls=src[src.index('void FcPlayer::sendInput()'):src.index('void FcPlayer::showMenu()')]
    keys=src[src.index('void FcPlayer::keys('):src.index('void FcPlayer::poll()')]
    (run/'fc_input.inc').write_text('namespace apps {\n'+controls+keys+'\n}\n',encoding='utf-8')
    widgets=src[src.index('void FcPlayer::createControls()'):src.index('void FcPlayer::releaseTilt()')]
    touch=src[src.index('void FcPlayer::touchEvent('):src.index('void FcPlayer::sendInput()')]
    (run/'fc_controls.inc').write_text('namespace apps {\n'+widgets+touch+'\n}\n',encoding='utf-8')
    compact=(ROOT/'main/ui/compact_ui.cpp').read_text(encoding='utf-8')
    (run/'compact_ui.inc').write_text(re.sub(r'^#include .*\n','',compact,flags=re.M),encoding='utf-8')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...)\n#define ESP_LOGW(...)\n')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 258\n#define ESP_ERR_INVALID_STATE 259\n#define ESP_ERR_NOT_FOUND 261\n#define ESP_ERR_TIMEOUT 263\n')
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h')
    manifest=json.loads((cache/'c-objects.json').read_text());objects=[]
    for n,digest in manifest['inputs'].items():assert sha(cache/n if n=='lv_conf.h' else ROOT/n)==digest,n
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        shutil.copyfile(cache/n,run/n);objects.append(run/n)
    def execute(name,args,compiler=cc):
        rsp=run/(name+'.rsp');rsp.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),encoding='utf-8')
        p=subprocess.run([str(compiler),'@'+str(rsp)],cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode(errors='replace')[-6000:]
    flags=['-std=c++17','-O1','-DLV_FONT_MONTSERRAT_10=1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui')]
    # The quick-panel cache includes this unit with font 10 disabled. Rebuild
    # its local copy for the Bluetooth address row, preserving the shared cache.
    execute('address-font',['-std=c11']+flags[1:]+['-c',ROOT/'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_10.c'],Path(__import__('shutil').which('gcc') or 'gcc').resolve())
    execute('compile',flags+['-c',ROOT/'../tooling/firmware/native/fc_menu_ui_replay.cpp',ROOT/'main/ui/theme.cpp'])
    exe=run/'fc-menu-ui.exe';execute('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects+[run/'fc_menu_ui_replay.o',run/'theme.o'])
    p=subprocess.run([str(exe)],cwd=run,env=env,capture_output=True,timeout=60,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode(errors='replace')
    result=json.loads(p.stdout);result.update(source_sha256=sha(ROOT/'main/apps/fc_player.cpp'),device_accessed=False)
    (run/'result.json').write_text(json.dumps(result,indent=2));print(p.stdout.decode().strip())
if __name__=='__main__':main()
