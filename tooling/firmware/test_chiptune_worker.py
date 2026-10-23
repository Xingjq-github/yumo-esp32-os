"""Actual chiptune worker with thread/audio/admission doubles; fixture-only I/O."""
from pathlib import Path
import argparse,os,subprocess,json,re,hashlib
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def execute(name,args):
        result=subprocess.run([str(v) for v in args],cwd=run,env=env,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-8000:])
        return result.stdout.decode('utf-8')
    worker=ROOT/'main/core/chiptune_player.cpp';body=re.sub(r'^#include .*\n','',worker.read_text(encoding='utf-8'),flags=re.M)
    body=body.replace('std::fopen','fake::open').replace('std::fclose','fake::close').replace('opendir(', 'fake::directory(').replace('decltype(&closedir)','decltype(&fake::closeDirectory)').replace(',closedir)',',fake::closeDirectory)').replace('stat(', 'fake::info(')
    (run/'worker.inc').write_text(body,encoding='utf-8')
    folder=run/'fixtures/chiptune';folder.mkdir(parents=True)
    score=dict(format='chip-score',version=1,title='fixture',tempo=120,loop=True,tracks=[dict(id='a',wave='pulse',notes='O4 L4 C D E F G A B >C')])
    for i in range(205):(folder/f'{i:03}.chip.json').write_text('{bad' if i==1 else json.dumps(score))
    (folder/'ignored.txt').write_text('ignore');(folder/'folder.chip.json').mkdir()
    cj=ROOT/'../dependencies/managed_components/espressif__cjson/cJSON'
    execute('cjson',[cc.with_name('gcc.exe'),'-O2','-c',cj/'cJSON.c','-o','cJSON.o'])
    execute('compile',[cc,'-std=c++17','-O1','-Wall','-Wextra','-I'+str(cj),'-I'+str(ROOT/'main'),'-I'+str(run),ROOT/'../tooling/firmware/native/chiptune_worker_test.cpp',ROOT/'main/core/chiptune_score.cpp','cJSON.o','-o','worker.exe','-static-libgcc','-static-libstdc++'])
    output=execute('run',[run/'worker.exe',run/'fixtures'])
    inputs=[worker,ROOT/'main/core/chiptune_player.hpp',ROOT/'main/core/chiptune_score.cpp',ROOT/'main/core/chiptune_score.hpp',ROOT/'../tooling/firmware/native/chiptune_worker_test.cpp']
    result=dict(passed=True,output=output,inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},hardware_audio_tested=False,real_sd_accessed=False)
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
