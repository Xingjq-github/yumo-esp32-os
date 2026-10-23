"""Host execution of production checkpoint/file IO with synthetic ROMs."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--run-dir',type=Path,required=True)
    args=p.parse_args();root=(Path(__file__).resolve().parents[2]/'firmware');run=args.run_dir.resolve()
    run.mkdir(parents=True,exist_ok=True);media=run/'synthetic-sd';media.mkdir(exist_ok=False)
    engine=root/'components/nofrendo'
    names=['components/nofrendo/'+s for s in re.findall(r'"([^"\n]+\.c)"',(engine/'CMakeLists.txt').read_text()) if s!='fc_port.c']
    names+=['main/core/vault_sha256.c','main/core/fc_save_store.c','../tooling/firmware/native/fc_checkpoint_runtime.c']
    exe=run/'checkpoint.exe'
    # TinyCC's older MSVCRT header lacks these POSIX errno values. Firmware
    # uses Newlib's own errno definitions; the host shim is test-only.
    cmd=[str(args.compiler.resolve()),'-Icomponents/nofrendo','-Imain','-Dfree=probe_free',
         '-DECANCELED=125','-DESTALE=116','-DEBADMSG=74','-DEOVERFLOW=75','-o',str(exe)]+names
    results=[]
    cold=run/'cold-sd';cold.mkdir()
    for name,command in [('compile',cmd),('execute',[str(exe),str(media)]),
                         ('cold-write',[str(exe),str(cold),'write']),('cold-read',[str(exe),str(cold),'read'])]:
        result=subprocess.run(command,cwd=root,capture_output=True,timeout=180)
        (run/(name+'.stdout.log')).write_bytes(result.stdout);(run/(name+'.stderr.log')).write_bytes(result.stderr)
        results.append({'stage':name,'command':command,'exit_code':result.returncode})
        if result.returncode:
            print(result.stdout.decode(errors='replace'));print(result.stderr.decode(errors='replace'))
            raise RuntimeError(name+' failed')
    files=list(engine.rglob('*.c'))+list(engine.rglob('*.h'))+[root/n for n in names[-3:]]+[root/'main/core/vault_sha256.h',root/'main/core/fc_save_store.h',root/'../tooling/firmware/native/mmc3_variants_runtime.c',Path(__file__).resolve()]
    report={'passed':True,'stages':results,'result':result.stdout.decode().strip(),
            'inputs':{s.resolve().relative_to(root.parent).as_posix():hashlib.sha256(s.read_bytes()).hexdigest() for s in files}}
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(report['result'])
if __name__=='__main__':main()
