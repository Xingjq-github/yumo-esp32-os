"""Compile production LAN browser handlers with host platform adapters; synthetic data only."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--run-dir',type=Path,required=True);run=parser.parse_args().run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    def adapt(name,target):
        text=(ROOT/name).read_text(encoding='utf-8')
        text=re.sub(r'^#include .*\n|^#pragma once\n|^extern const unsigned char .*\n','',text,flags=re.M)
        text=text.replace('"/sdcard"','"./sdcard"') # host VFS mount maps to isolated run directory
        (run/target).write_text(text,encoding='utf-8')
    for source,target in [('core/lan_web.cpp','lan_web.inc'),('core/lan_web_protocol.hpp','lan_web_protocol.inc'),('core/vault_text.hpp','vault_text.inc')]:adapt('firmware/main/'+source,target)
    # Use the bundled lwIP layouts; the old address-only socket fake hid the
    # IPv4-mapped IPv6 result returned by this firmware's dual-stack HTTP server.
    sdk=ROOT/'dependencies/esp-idf/components/lwip/lwip/src/include/lwip'
    inet=(sdk/'inet.h').read_text(encoding='utf-8');sockets=(sdk/'sockets.h').read_text(encoding='utf-8')
    types=inet[inet.index('struct in_addr {'):inet.index('/** 255.255.255.255 */')]
    types+=sockets[sockets.index('#if LWIP_IPV4\n/* members are in network byte order */'):sockets.index('/* If your port already typedef\'s socklen_t')]
    (run/'socket_types.inc').write_text(types,encoding='utf-8')
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve();cjson=ROOT/'dependencies/managed_components/espressif__cjson/cJSON'
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    commands=[('compile',[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-I'+str(run),'-I'+str(ROOT/'firmware/main'),'-I'+str(cjson),str(ROOT/'tooling/firmware/native/lan_web_replay.cpp'),str(ROOT/'firmware/main/core/file_operations.cpp'),str(cjson/'cJSON.c'),'-o',str(run/'web.exe'),'-static-libgcc','-static-libstdc++']),('replay',[str(run/'web.exe')])]
    for name,args in commands:
        result=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode('utf-8',errors='replace'))
    names=['firmware/main/core/lan_web.cpp','firmware/main/core/lan_web_protocol.hpp','firmware/main/core/vault_text.hpp',
           'firmware/main/core/file_operations.cpp','tooling/firmware/test_lan_web_runtime.py','tooling/firmware/native/lan_web_replay.cpp']
    names += [(sdk/name).relative_to(ROOT).as_posix() for name in ('inet.h','sockets.h')]
    report=dict(passed=True,result=result.stdout.decode().strip(),hardware=False,production_handler=True,sd='isolated synthetic host directory',
                socket_layout='Bundled lwIP sockaddr structures with synthetic IPv4/IPv4-mapped IPv6 endpoints',
                inputs={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names})
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report['result'])
if __name__=='__main__':main()
