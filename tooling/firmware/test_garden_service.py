"""Compile the production garden worker with deterministic RTOS/SD boundaries."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--resume',action='store_true');a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=a.resume)
    files={'esp_err.h':'#pragma once\nusing esp_err_t=int;\n','esp_heap_caps.h':'#pragma once\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n',
    'freertos/FreeRTOS.h':'#pragma once\n#define pdPASS 1\n','freertos/task.h':'#pragma once\n',
    'freertos/idf_additions.h':'#pragma once\nint xTaskCreateWithCaps(void (*)(void *),const char *,unsigned,void *,unsigned,void *,unsigned);\nvoid vTaskDeleteWithCaps(void *);\n'}
    for name,source in files.items():p=run/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(source)
    cc=a.compiler.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    main=ROOT/'firmware/main';sources=[main/'core/garden_service.cpp',main/'core/garden_model.cpp',ROOT/'tooling/firmware/garden_service_host.cpp']
    cmd=[str(cc),'-std=c++17','-O0','-g','-DESTALE=116','-I'+str(run),'-I'+str(main)]+[str(p) for p in sources]+['-o',str(run/'service.exe')]
    for stage,args in [('compile',cmd),('execute',[str(run/'service.exe')])]:
        p=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=60);(run/(stage+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:print((p.stdout+p.stderr).decode('utf8','replace'));raise RuntimeError(stage+' failed')
    print(p.stdout.decode('utf8','replace'))
    (run/'result.json').write_text(json.dumps({'passed':True,'firmware_build':False,'flash':False,'inputs':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[Path(__file__)]}},indent=2))
if __name__=='__main__':main()
