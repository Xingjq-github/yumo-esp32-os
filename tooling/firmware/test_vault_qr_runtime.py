"""Execute production QR renderer and independently decode its 240px bitmaps."""
from pathlib import Path
import argparse,json,os,re,subprocess,sys,hashlib
ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);p.add_argument('--decoder',type=Path,required=True);args=p.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    for name,out in [('core/vault_text.hpp','vault_text.inc'),('ui/vault_qr.cpp','vault_qr.inc')]:
        text=(ROOT/'firmware/main'/name).read_text(encoding='utf-8');text=re.sub(r'^#include .*\n|^#pragma once\n','',text,flags=re.M);(run/out).write_text(text,encoding='utf-8')
    qr=ROOT/'dependencies/managed_components/lvgl__lvgl/src/libs/qrcode'
    header=(qr/'qrcodegen.h').read_text(encoding='utf-8').replace('#include "../../../lvgl.h"','#define LV_USE_QRCODE 1\n#define LV_STDBOOL_INCLUDE <stdbool.h>\n#define LV_STDDEF_INCLUDE <stddef.h>\n#define LV_STDINT_INCLUDE <stdint.h>')
    (run/'qrcodegen.h').write_text(header,encoding='utf-8')
    text=(qr/'qrcodegen.c').read_text(encoding='utf-8').replace('#include "../../lvgl_public.h"','#define LV_USE_QRCODE 1\n#define LV_LIMITS_INCLUDE <limits.h>\n#include <assert.h>\n#define LV_ASSERT assert')
    (run/'qrcodegen.c').write_text(text,encoding='utf-8')
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    commands=[('encoder',[str(cc.with_name('gcc.exe')),'-O1','-I'+str(run),'-c',str(run/'qrcodegen.c'),'-o',str(run/'qrcodegen.o')]),('compile',[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-I'+str(run),'-I'+str(qr),str(ROOT/'tooling/firmware/native/vault_qr_replay.cpp'),str(run/'qrcodegen.o'),'-o',str(run/'qr.exe'),'-static-libgcc','-static-libstdc++']),('render',[str(run/'qr.exe')])]
    for name,command in commands:
        result=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));(run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode('utf-8',errors='replace'))
    sys.path.insert(0,str(args.decoder))
    from PIL import Image
    import zxingcpp
    decoded=[]
    for name in ('typical','code','maximum','unicode'):
        image=Image.open(run/(name+'.pgm'));image.save(run/(name+'.png'))
        result=zxingcpp.read_barcode(image)
        expected=(run/(name+'.txt')).read_text(encoding='utf-8')
        assert result and result.text==expected,name
        decoded.append(dict(name=name,bytes=len(expected.encode()),exact=True,format=str(result.format)))
    report=dict(passed=True,decoder='zxing-cpp 2.3.0',cases=decoded,real_camera_test=False,production_canvas=True)
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
