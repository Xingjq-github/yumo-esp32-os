"""Fetch pinned component packages; tools and SDK must be installed separately."""
from pathlib import Path
import hashlib, io, json, shutil, tempfile, urllib.request, zipfile

ROOT=Path(__file__).resolve().parents[1]
STORAGE='https://components-file.espressif.com/'

def fetch(path):
    request=urllib.request.Request(STORAGE+path, headers={'User-Agent':'Yumo-source-build/1'})
    with urllib.request.urlopen(request, timeout=90) as response:
        return response.read()

def digest(data):
    return hashlib.sha256(data).hexdigest()

def verify(folder, checks, overlays):
    for entry in checks['files']:
        relative=Path(entry['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('Invalid checksum path')
        if '.git' in relative.parts:
            continue
        expected=overlays.get(entry['path'], entry['hash'])
        file=folder/relative
        if not file.is_file() or digest(file.read_bytes()) != expected:
            raise RuntimeError('Component content differs: '+entry['path'])

def main():
    config=json.loads((ROOT/'config/dependencies.json').read_text())
    target=ROOT/'dependencies/managed_components'
    target.mkdir(parents=True, exist_ok=True)
    for item in config['components']:
        name=item['name'];version=item['version'];folder_name=name.replace('/','__')
        folder=target/folder_name
        expected={row['path']:row['after'] for row in item.get('overlays',[])}
        if folder.exists():
            raw=(folder/'CHECKSUMS.json').read_bytes()
            if digest(raw)!=item['checksums_sha256']:
                raise RuntimeError('Wrong component version: '+name)
            verify(folder,json.loads(raw),expected)
            print(name+' verified',flush=True)
            continue
        print('Downloading '+name+' '+version,flush=True)
        metadata=json.loads(fetch('components/'+name+'.json'))
        record=next((v for v in metadata['versions'] if v['version']==version),None)
        if record is None:
            raise RuntimeError('Pinned version not available: '+name+' '+version)
        raw=fetch(record['checksums'])
        if digest(raw)!=item['checksums_sha256']:
            raise RuntimeError('Published checksums changed: '+name)
        checks=json.loads(raw)
        with tempfile.TemporaryDirectory(prefix='.download-',dir=target) as temporary:
            stage=Path(temporary)
            with zipfile.ZipFile(io.BytesIO(fetch(record['url']))) as archive:
                for entry in archive.infolist():
                    relative=Path(entry.filename)
                    if relative.is_absolute() or '..' in relative.parts or ':' in entry.filename or '\\' in entry.filename:
                        raise RuntimeError('Unsafe component archive path')
                    if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                        raise RuntimeError('Symlinks are not supported in component packages')
                    if '.git' in relative.parts:
                        continue
                    archive.extract(entry,stage)
            verify(stage,checks,{})
            for row in item.get('overlays',[]):
                output=stage/row['path'];patch=ROOT/'patches/components'/folder_name/row['path']
                if digest(output.read_bytes())!=row['before'] or digest(patch.read_bytes())!=row['after']:
                    raise RuntimeError('Component overlay does not match baseline')
                shutil.copyfile(patch,output)
            (stage/'CHECKSUMS.json').write_bytes(raw)
            verify(stage,checks,expected)
            # Atomic publication prevents an interrupted download from appearing complete.
            stage.rename(folder)
        print(name+' ready',flush=True)

if __name__=='__main__':
    main()
