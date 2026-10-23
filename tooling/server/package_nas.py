"""Package a built/tested JAR for a user-provided Java image. No Docker required."""
import argparse,datetime,hashlib,json,shutil,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
SERVER=Path(__file__).resolve().parents[2]/'server'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--verification',required=True,type=Path);args=parser.parse_args()
 jar=SERVER.parent/'artifacts/server/joyvault.jar';nas=SERVER.parent/'packaging/nas';release=SERVER.parent/'artifacts/releases/nas/release';archive=release.parent/('joyvault-nas-java8-'+datetime.date.today().strftime('%Y%m%d')+'.zip')
 report=json.loads(args.verification.read_text(encoding='utf-8-sig'))
 if not report.get('passed') or report.get('jar_sha256')!=sha(jar):raise SystemExit('Verification does not match this JAR')
 # Maven can reuse the same JAR when only tests change. Production inputs
 # must predate the JAR; test inputs must predate the successful test reports.
 for path in [SERVER/'pom.xml',SERVER/'vault-sync-key.properties',*SERVER.joinpath('src/main').rglob('*')]:
  if path.is_file() and path.stat().st_mtime>jar.stat().st_mtime:raise SystemExit('Sources changed after JAR build; rebuild and reverify')
 suites=list((SERVER.parent/'artifacts/server/surefire-reports').glob('TEST-*.xml'))
 if not suites:raise SystemExit('JUnit reports missing')
 oldest_test_report=min(path.stat().st_mtime for path in suites)
 if any(path.is_file() and path.stat().st_mtime>oldest_test_report for path in SERVER.joinpath('src/test').rglob('*')):
  raise SystemExit('Tests changed after JUnit execution; rebuild and reverify')
 for path in suites:
  suite=ET.parse(path).getroot()
  if int(suite.attrib.get('failures','0')) or int(suite.attrib.get('errors','0')):raise SystemExit('JUnit not successful')
 with zipfile.ZipFile(jar) as z:
  names=z.namelist()
  if 'BOOT-INF/classes/com/joyvault/VaultApplication.class' not in names:raise SystemExit('Not an executable JoyVault JAR')
  config='BOOT-INF/classes/vault-sync-key.properties'
  if config not in names or z.read(config)!=(SERVER/'vault-sync-key.properties').read_bytes():raise SystemExit('Packaged sync configuration is stale; rebuild and reverify')
  if any('ProtocolFixtureCli' in n or n.endswith(('vault.db','local-keys.json')) for n in names):raise SystemExit('Private/test data unexpectedly included in JAR')
 allowed={'joyvault.jar','compose.yaml','.env.example','README.md','verification.json','deployment/local-keys.json','SHA256SUMS'}
 if release.exists():
  unexpected={p.relative_to(release).as_posix() for p in release.rglob('*') if p.is_file()}-allowed
  if unexpected:raise SystemExit('Unexpected files in generated release directory; refusing overwrite')
 release.mkdir(parents=True,exist_ok=True);(release/'deployment').mkdir(exist_ok=True)
 shutil.copyfile(jar,release/'joyvault.jar');shutil.copyfile(SERVER/'deployment/local-keys.json',release/'deployment/local-keys.json')
 for name in ['compose.yaml','.env.example']:
  (release/name).write_text((nas/name).read_text(encoding='utf-8-sig'),encoding='utf-8',newline='\n')
 (release/'README.md').write_text((SERVER.parent/'docs/server/nas.md').read_text(encoding='utf-8-sig'),encoding='utf-8',newline='\n')
 (release/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 files=sorted(allowed-{'SHA256SUMS'})
 (release/'SHA256SUMS').write_text(''.join(sha(release/f)+'  '+f+'\n' for f in files),encoding='ascii')
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for name in sorted(allowed):z.write(release/name,name)
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None and set(z.namelist())==allowed
  for name in files:assert hashlib.sha256(z.read(name)).hexdigest()==sha(release/name)
 (archive.parent/(archive.name+'.sha256')).write_text(sha(archive)+'  '+archive.name+'\n',encoding='ascii')
 print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,'jar_sha256':sha(jar),'contains_database':False,'contains_image':False,'fixed_keys_included':True}))
if __name__=='__main__':main()
