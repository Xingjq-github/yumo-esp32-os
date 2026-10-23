"""Apply the bounded NimBLE lifecycle fix to the user's pinned SDK checkout."""
from pathlib import Path
import hashlib, json, subprocess
from environment import ROOT, sdk_path

def check(sdk, apply=False):
    spec=json.loads((ROOT/'config/dependencies.json').read_text())['sdk']
    row=spec['patch'];base=Path(sdk)/row['submodule'];file=base/row['file']
    digest=hashlib.sha256(file.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
    if digest==row['after']:
        return
    if digest!=row['before']:
        raise RuntimeError('NimBLE differs from the pinned SDK; refusing to change it.')
    if not apply:
        raise RuntimeError('Run python tooling/apply_sdk_patch.py before building.')
    commit=subprocess.check_output(['git','-C',str(base),'rev-parse','HEAD'],text=True).strip()
    if commit!=row['commit']:
        raise RuntimeError('Unexpected NimBLE submodule revision.')
    args=['git','-C',str(base),'apply','--ignore-space-change',str(ROOT/row['patch'])]
    subprocess.run(args[:4]+['--check']+args[4:],check=True)
    subprocess.run(args,check=True)
    check(sdk)

if __name__=='__main__':
    check(sdk_path(),apply=True)
    print('Pinned NimBLE lifecycle fix verified.')
