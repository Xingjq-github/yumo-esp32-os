"""Exercise production LAN state machine with injected radio/netif behavior."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,shutil
ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,help='Host g++ path; defaults to PATH')
    p.add_argument('--run-dir',type=Path,required=True)
    args=p.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    source=ROOT/'firmware/main/core/lan_hotspot.cpp'
    (run/'lan_hotspot.inc').write_text(re.sub(r'^#include .*\n','',source.read_text(encoding='utf-8'),flags=re.M),encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_NOT_FOUND=0x105,ESP_ERR_NOT_FINISHED=0x10c,ESP_ERR_TIMEOUT=0x107,ESP_ERR_WIFI_NOT_STARTED=0x3002,ESP_ERR_WIFI_NOT_INIT=0x3001,ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED=0x5001,ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED=0x5002;\n',encoding='utf-8')
    selected=args.compiler or shutil.which('g++')
    if not selected:raise SystemExit('Install host g++ on PATH or pass --compiler')
    cc=Path(selected).resolve()
    if not cc.is_file():raise SystemExit('Host compiler not found: '+str(cc))
    native=ROOT/'tooling/firmware/native/lan_share_replay.cpp'
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    command=[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(run),'-I'+str(ROOT/'firmware/main'),str(native),'-o',str(run/'lan.exe'),'-static-libgcc','-static-libstdc++']
    for name,args in [('compile',command),('replay',[str(run/'lan.exe')])]:
        result=subprocess.run(args,cwd=run,env=env,capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode('utf-8',errors='replace'))
    files=[source,native,Path(__file__)]+[ROOT/'firmware/main/core'/n for n in ('lan_hotspot.hpp','lan_hotspot_model.hpp','lan_routing.hpp')]
    report=dict(passed=True,result=result.stdout.decode('utf-8').strip(),hardware_test=False,actual_packet_forwarding_test=False,inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report['result'])
if __name__=='__main__':main()
