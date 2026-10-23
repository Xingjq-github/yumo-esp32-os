"""Exercise the real Morse model, waveform and LVGL UI with fake hardware."""
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
    inputs={n:sha(ROOT/n) for n in ('main/apps/morse.cpp','main/apps/morse.hpp','main/apps/morse_model.hpp',
        'main/core/morse_signal.hpp','main/core/game_audio.hpp','main/core/game_audio.cpp','main/core/game_synth.hpp',
        'main/ui/theme.cpp','../tooling/firmware/native/morse_replay.cpp','main/ui/lv_font_menu_14.c')}
    code=(ROOT/'main/apps/morse.cpp').read_text(encoding='utf-8')
    (run/'morse.inc').write_text(re.sub(r'^#include .*\n','',code,flags=re.M),encoding='utf-8')
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
    flags=['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(ROOT/'../dependencies/managed_components/lvgl__lvgl'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/ui')]
    if changed:execute('updated-font',[str(compiler.with_name('gcc.exe')),rsp('updated-font',flags+['-std=c11','-c']+changed)])
    (run/'c-objects.json').write_text(json.dumps({'inputs':updated,'objects':{p.name:sha(p) for p in objects}}))
    execute('compile',[str(compiler),rsp('compile',flags+['-std=c++17','-c',ROOT/'../tooling/firmware/native/morse_replay.cpp',ROOT/'main/ui/theme.cpp'])])
    exe=run/'morse.exe';execute('link',[str(compiler),rsp('link',['-static-libgcc','-static-libstdc++','-o',exe]+objects+[run/'morse_replay.o',run/'theme.o'])])
    output=execute('run',[str(exe)])
    result=dict(passed=True,ui=output,inputs=inputs,synthetic_only=True,device_accessed=False,
        method='Production model, PCM signal, App and local LVGL; power, navigation and audio worker are test doubles; real speaker requires device listening')
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
