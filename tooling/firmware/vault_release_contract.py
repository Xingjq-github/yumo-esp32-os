"""Current authorized release checks; old source freezes allow only marker replacement."""
from pathlib import Path
from functools import lru_cache
import hashlib,json
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/vault-pin-build-flash-20260918-1710'
MARKER='VAULT-PIN-20260918'
MARKER_FILES={'main/main.cpp','main/apps/settings_system.cpp'}
def source_before_release(name):
    raw=(ROOT/name).read_bytes()
    if name in MARKER_FILES:
        assert raw.count(MARKER.encode())==1,name
        return raw.replace(MARKER.encode(),b'VAULT-ENTER-20260918')
    return raw
@lru_cache(maxsize=1)
def current_receipt():
    path=RUN/'build_receipt.json'
    if not path.exists():return None
    value=json.loads(path.read_text(encoding='utf-8'))
    assert value['marker']==MARKER
    assert value['build']['status']=='succeeded' and value['build']['exit_code']==0
    assert Path(value['build']['build']).resolve()==ROOT/'../artifacts/firmware'
    inputs=json.loads((RUN/'build_inputs.json').read_text())['files']
    assert len(inputs)==477
    for name,item in inputs.items():
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==item['sha256'],name
    return value

def expected_build_hash(name,fallback):
    receipt=current_receipt()
    return receipt['generated_build_files'][name]['sha256'] if receipt else fallback
