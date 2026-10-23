"""Independent mission contracts, negative paths and bounded hint recovery."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',required=True,type=Path);ap.add_argument('--run-dir',required=True,type=Path)
    a=ap.parse_args();run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    contract=json.loads((ROOT/'../tooling/firmware/fixtures/pattern_missions.json').read_text(encoding='utf-8'))
    packed=json.loads((ROOT/'../artifacts/resources/patterns/lessons.json').read_text(encoding='utf-8'))
    lines=['static void contracts(){Game g;']
    for case in contract:
        i,s=case['id'],case['stage'];assert packed[i]['runs'][s]['output']==case['output']
        lines.append(f'g.reset({i},{s});')
        for action in case['actions']:lines.append(f'g.act({action});')
        for key,value in case['state'].items():lines.append(f'CHECK(g.v[{key}]=={value});')
        lines.append('CHECK(g.won);')
    lines.append('}');(run/'contract.inc').write_text('\n'.join(lines))
    env=dict(os.environ);env['PATH']=str(a.compiler.resolve().parent)+os.pathsep+env.get('PATH','')
    exe=run/'semantics.exe'
    commands=[[str(a.compiler.resolve()),'-std=c++17','-O1','-static-libgcc','-static-libstdc++','-I'+str(ROOT/'main'),'-I'+str(run),str(ROOT/'../tooling/firmware/native/pattern_semantics.cpp'),'-o',str(exe)],[str(exe)]]
    for i,cmd in enumerate(commands):
        p=subprocess.run(cmd,env=env,cwd=run,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/f'{i}.log').write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
    result=dict(passed=True,missions=len(contract),output=p.stdout.decode().strip(),device_accessed=False,
        inputs={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['main/core/pattern_play.hpp','../tooling/firmware/fixtures/pattern_missions.json','../tooling/firmware/native/pattern_semantics.cpp','../artifacts/resources/patterns/lessons.json']})
    (run/'result.json').write_text(json.dumps(result,indent=2));print(result['output'])
if __name__=='__main__':main()
