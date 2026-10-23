"""Real MP3 worker and decoder; synthetic SD/RTOS/outputs, no device access."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--decoder-run',type=Path,required=True);ap.add_argument('--fixture',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    source=ROOT/'firmware/main/core/mp3_player.cpp';code=re.sub(r'^#include .*\n','',source.read_text(encoding='utf8'),flags=re.M)
    code=code.replace('std::fclose','fake::close').replace('fopen(', 'fake::open(').replace(',fclose)',',fake::close)').replace('stat(', 'fake::info(').replace('opendir(', 'fake::directory(').replace('&closedir','&fake::closeDir').replace(',closedir)',',fake::closeDir)')
    code=code.replace('fread(', 'fake::read(')
    (run/'worker.inc').write_text(code,encoding='utf8')
    folder=run/'fixtures/MP3';folder.mkdir(parents=True)
    for i in range(520):(folder/f'{i:03}.mp3').write_bytes(a.fixture.read_bytes() if i==0 else b'bad')
    (folder/'bad.mp3').write_bytes(b'not an MP3');(folder/'ignore.wav').write_bytes(b'');(folder/'directory.mp3').mkdir()
    (folder/'000.lrc').write_text('[0:00]first\n[0:01]second',encoding='utf8')
    flags=['-O1','-std=c++17','-Wall','-Wextra','-DMINIMP3_NO_STDIO','-DMINIMP3_ONLY_MP3','-DMINIMP3_NO_SIMD','-I'+str(run),'-I'+str(ROOT/'firmware/main'),'-I'+str(ROOT/'firmware/components/mp3_decoder/include'),'-I'+str(ROOT/'dependencies/managed_components/espressif__esp_audio_effects/include')]
    def call(name,args):
        p=subprocess.run([str(x) for x in args],cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));(run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-5000:])
        return p.stdout.decode()
    call('compile',[cc,*flags,ROOT/'tooling/firmware/native/mp3_worker_test.cpp',a.decoder_run.resolve()/'decoder.o','-static-libgcc','-static-libstdc++','-o','worker.exe'])
    output=call('run',[run/'worker.exe',run/'fixtures']);result=dict(passed=True,output=output,worker_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),physical_audio_tested=False)
    (run/'result.json').write_text(json.dumps(result,indent=2));print(output)
if __name__=='__main__':main()
