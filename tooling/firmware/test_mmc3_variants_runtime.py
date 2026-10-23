"""Build/run the real NES engine on the host; no external ROMs or device writes."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--run-dir',type=Path,required=True)
    args=p.parse_args()
    root=(Path(__file__).resolve().parents[2]/'firmware');run=args.run_dir.resolve()
    run.mkdir(parents=True,exist_ok=True)
    engine=root/'components/nofrendo'
    sources=[engine/s for s in re.findall(r'"([^"\n]+\.c)"',(engine/'CMakeLists.txt').read_text()) if s!='fc_port.c']
    sources.append(root/'../tooling/firmware/native/mmc3_variants_runtime.c')
    exe=run/'mmc3_variants_runtime.exe'
    cmd=[str(args.compiler.resolve()),'-I'+str(engine),'-Dfree=probe_free','-o',str(exe)]+list(map(str,sources))
    results=[]
    for name,command in [('compile',cmd),('execute',[str(exe)])]:
        result=subprocess.run(command,cwd=root,capture_output=True,timeout=60)
        (run/(name+'.stdout.log')).write_bytes(result.stdout)
        (run/(name+'.stderr.log')).write_bytes(result.stderr)
        results.append({'stage':name,'command':command,'exit_code':result.returncode})
        if result.returncode:
            print(result.stdout.decode(errors='replace'));print(result.stderr.decode(errors='replace'))
            raise RuntimeError(name+' failed')
    files=list(engine.rglob('*.c'))+list(engine.rglob('*.h'))+[sources[-1]]
    report={'passed':True,'stages':results,'result':result.stdout.decode().strip(),
            'inputs':{s.relative_to(root).as_posix():hashlib.sha256(s.read_bytes()).hexdigest() for s in files}}
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(report['result'])

if __name__=='__main__': main()
