"""Replay production photo view methods with deterministic UI/worker timing."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,os
ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(exist_ok=False)
    header=ROOT/'firmware/main/apps/photo_viewer.hpp';source=header.with_suffix('.cpp')
    h=re.sub(r'^#include .*\n','',header.read_text('utf-8'),flags=re.M)
    h=h.replace(': public ui::App','').replace(' override','').replace('private:','public:')
    (run/'view_header.inc').write_text(h,encoding='utf-8')
    s=source.read_text('utf-8')
    code=s[s.index('void PhotoViewer::pollImage()'):s.index('void PhotoViewer::releaseGif()')]
    code+=s[s.index('void PhotoViewer::queueView(bool pan)'):s.rindex('} // namespace apps')]
    (run/'view_methods.inc').write_text('namespace apps {\n'+code+'\n}',encoding='utf-8')
    cpp=ROOT/'tooling/firmware/native/photo_viewport_ui.cpp';exe=run/'ui.exe'
    args=[str(Path(__import__('shutil').which('g++') or 'g++').resolve()),'-std=c++17','-O2','-Wall','-Wextra','-static',
          '-I'+str(run),'-I'+str(ROOT/'firmware/main'),'-I'+str(ROOT/'firmware/components/photo_jpeg/include'),str(cpp),'-o',str(exe)]
    env=dict(os.environ);env['PATH']=str(Path(__import__('shutil').which('g++') or 'g++').resolve().parent)+os.pathsep+env.get('PATH','')
    c=subprocess.run(args,env=env,capture_output=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'compile.log').write_bytes(c.stdout+c.stderr);assert c.returncode==0,c.stderr.decode(errors='replace')
    c=subprocess.run([str(exe)],capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'replay.log').write_bytes(c.stdout+c.stderr);assert c.returncode==0,c.stderr.decode(errors='replace')
    result=json.loads(c.stdout);assert result['passed']
    paths=[source,header,cpp,Path(__file__),ROOT/'firmware/main/core/photo_loader.hpp',ROOT/'firmware/components/photo_jpeg/include/photo_view.h']
    result.update(inputs={x.relative_to(ROOT).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in paths},
                  method='Production PhotoViewer methods and geometry, synthetic LVGL/input and delayed worker results',device_accessed=False)
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
