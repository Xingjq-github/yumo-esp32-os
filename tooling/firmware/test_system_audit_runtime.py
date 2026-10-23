"""Production shutdown/cleanup/settings paths with fault injection; no device I/O."""
from pathlib import Path
import argparse,json,os,re,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True)
    p.add_argument('--case',action='append',choices=('system_shutdown','lan_cleanup','settings_commit'))
    args=p.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++').resolve()
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def part(file,start,end):
        text=(ROOT/file).read_text(encoding='utf-8');return text[text.index(start):text.index(end,text.index(start))]
    power=part('main/core/power_mgr.cpp','esp_err_t Power::shutdown()', 'void Power::standby(')
    power+=part('main/core/power_mgr.cpp','esp_err_t Power::deepSleep(', 'esp_err_t Power::restartForTheme(')
    power+=part('main/core/power_mgr.cpp','void Power::standby(', 'bool Power::sleepPending(')
    power+=part('main/core/power_mgr.cpp','void Power::processStandby()', 'void Power::syncTouch(')
    power+=part('main/core/power_runtime.cpp','bool Power::automaticDeepSleepAllowed()', 'bool Power::automaticScreenOffAllowed()')
    (run/'shutdown.inc').write_text(power,encoding='utf-8')
    (run/'cleanup.inc').write_text(part('main/core/lan_hotspot.cpp','bool cleanup()', 'void sample(').replace('=nullptr','=0'),encoding='utf-8')
    settings=(ROOT/'main/core/settings_store.cpp').read_text(encoding='utf-8')
    settings=re.sub(r'^#include .*\n','',settings,flags=re.M).replace('timeval tv','FakeTimeval tv').replace('settimeofday(', 'fakeSetTime(')
    (run/'settings.inc').write_text(settings,encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2;\n')
    outputs={}
    for name in args.case or ('system_shutdown','lan_cleanup','settings_commit'):
        cmd=[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-I'+str(run),'-I'+str(ROOT/'main'),str(ROOT/'../tooling/firmware/native'/(name+'_replay.cpp')),'-o',str(run/(name+'.exe')),'-static-libgcc','-static-libstdc++']
        result=subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'-compile.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(result.stderr.decode(errors='replace')[-6000:])
        result=subprocess.run([str(run/(name+'.exe'))],cwd=run,env=env,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
        outputs[name]=result.stdout.decode().strip()
    report=dict(passed=True,outputs=outputs,device_accessed=False,real_storage_accessed=False)
    (run/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
if __name__=='__main__':main()
