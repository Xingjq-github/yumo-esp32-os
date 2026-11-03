"""Exercise production vault submission/completion scheduling with real LVGL timers.

The worker is a controlled fake: this measures GUI scheduling only, not KDF
speed, flash latency or hardware frame time. No device or user data access.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include "lvgl.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
static unsigned checks=0, calls=0, starts=0, launched=0, completed=0, duration=0;
static void check(bool ok,int line){++checks;if(!ok){fprintf(stderr,"check at %d\n",line);exit(1);}}
#define CHECK(v) check((v),__LINE__)
namespace vault {
enum class StartResult {Started,Busy,NoMemory,Unavailable};
struct Job{std::atomic<bool> done{false};};
static StartResult next=StartResult::Started;
StartResult start(const std::shared_ptr<Job>&j){
    if(next==StartResult::Started){++starts;launched=lv_tick_get();j->done=duration==0;}
    return next;
}
}
namespace apps {
@CONSTANTS@
class PasswordVault {
public:
    bool m_visible=true, page=true, m_patternPending=false,m_redraw=false;
    std::string m_pin,m_message;
    std::shared_ptr<vault::Job> m_job;
    lv_timer_t *m_timer=nullptr;
    bool patternPage()const{return page;}
    void wipeField(){}
    void message(const char *s){m_message=s;}
    vault::StartResult launch(const std::shared_ptr<vault::Job>&job);
    static void patternSubmitted(void*,const char*,size_t);
    void complete(){@COMPLETE@}
};
@PRODUCTION@
}
static void poll(lv_timer_t *timer){
    ++calls;
    auto &a=*static_cast<apps::PasswordVault*>(lv_timer_get_user_data(timer));
    // Worker/page routing is deliberately fake; the submission, launch,
    // completion timer transitions and LVGL scheduler are production code.
    if(a.m_patternPending){a.m_patternPending=false;a.launch(std::make_shared<vault::Job>());}
    if(a.m_job && a.m_job->done.load()){completed=lv_tick_get();a.complete();}
}
int main(){
    lv_init();lv_tick_inc(1);
    unsigned maxStart=0,maxComplete=0,maxOverhead=0,scenarios=0;
    unsigned long long totalOverhead=0;
    const unsigned durations[]={0,1,19,20,37,99,100,251,1000};
    for(unsigned phase=0;phase<100;++phase)for(unsigned delay:durations){
        apps::PasswordVault a;duration=delay;starts=calls=launched=completed=0;
        a.m_timer=lv_timer_create(poll,100,&a);
        lv_tick_inc(phase);const auto released=lv_tick_get();
        apps::PasswordVault::patternSubmitted(&a,"0125",4);
        CHECK(a.m_patternPending && !a.m_job && starts==0 && calls==0);
        // A duplicated release before the next timer pass must not replace it.
        apps::PasswordVault::patternSubmitted(&a,"3458",4);CHECK(a.m_pin=="0125");
        for(unsigned step=0;!completed && step<1300;++step){
            if(a.m_job && lv_tick_get()-launched>=delay)a.m_job->done=true;
            lv_timer_handler();
            if(!completed)lv_tick_inc(1);
        }
        CHECK(starts==1 && completed && !a.m_job && !a.m_patternPending);
        auto startWait=launched-released,completeWait=completed-launched-delay;
        maxStart=std::max(maxStart,startWait);maxComplete=std::max(maxComplete,completeWait);
        maxOverhead=std::max(maxOverhead,startWait+completeWait);totalOverhead+=startWait+completeWait;++scenarios;
        // Completion must restore the idle cadence, not keep 50Hz polling.
        const auto oldCalls=calls;
        for(unsigned i=0;i<300;++i){lv_tick_inc(1);lv_timer_handler();}
        CHECK(calls-oldCalls==3);
        lv_timer_delete(a.m_timer);
    }
    apps::PasswordVault a;
    a.m_timer=lv_timer_create(poll,100,&a);
    for(int state=0;state<3;++state){
        a.m_visible=state!=0;a.page=state!=1;
        a.m_job=state==2?std::make_shared<vault::Job>():nullptr;
        apps::PasswordVault::patternSubmitted(&a,"0125",4);CHECK(!a.m_patternPending && a.m_pin.empty());
    }
    a.m_visible=a.page=true;a.m_job.reset();vault::next=vault::StartResult::Busy;
    CHECK(a.launch(std::make_shared<vault::Job>())==vault::StartResult::Busy && !a.m_job);
    vault::next=vault::StartResult::Started;
    apps::PasswordVault::patternSubmitted(&a,"0125",4);
    const auto oldStarts=starts;
    lv_timer_delete(a.m_timer);a.m_timer=nullptr; // onHide's lifetime boundary
    lv_tick_inc(500);lv_timer_handler();CHECK(starts==oldStarts);
    printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"max_submit_wait_ms\":%u,\"max_completion_wait_ms\":%u,\"max_added_wait_ms\":%u,\"mean_added_wait_ms\":%.3f}\n",checks,scenarios,maxStart,maxComplete,maxOverhead,double(totalOverhead)/scenarios);
    lv_deinit();
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--lvgl-root', type=Path, required=True, help='External pinned LVGL source directory')
    parser.add_argument('--c-cache', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    args = parser.parse_args()
    run, cache = args.run_dir.resolve(), args.c_cache.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    assert cache.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(cache/'lv_conf.h', run/'lv_conf.h')
    lv = args.lvgl_root.resolve()
    files = [f for f in (lv/'src').rglob('*.c') if not set(f.relative_to(lv/'src').parts)&{'libs','drivers'}]
    files += list((lv/'src/libs/bin_decoder').glob('*.c'))
    objects = []
    for file in files:
        obj = cache/(file.stem+'.o')
        assert obj.is_file() and obj.stat().st_mtime_ns >= file.stat().st_mtime_ns
        objects.append(obj)
    source_file = ROOT/'firmware/main/apps/password_vault.cpp'
    current = source_file.read_text(encoding='utf8')
    constants = '\n'.join(re.findall(r'constexpr uint32_t (?:IdlePollMs|JobPollMs) = \d+;', current))
    assert len(constants.splitlines()) == 2
    env = dict(os.environ)
    env['PATH'] = str(args.compiler.resolve().parent)+os.pathsep+env.get('PATH','')
    results = {}
    for name, source in [('baseline', args.baseline.read_text(encoding='utf8')), ('updated',current)]:
        production = '\n'.join(function(source,s) for s in (
            'vault::StartResult PasswordVault::launch(', 'void PasswordVault::patternSubmitted('))
        completion = re.findall(r'    m_job.reset\(\);\n(?:    if \(m_timer\)\n      lv_timer_set_period\(m_timer, IdlePollMs\);\n)?    m_redraw = true;', function(source,'void PasswordVault::poll('))
        assert len(completion) == 1
        harness = HARNESS.replace('@CONSTANTS@',constants).replace('@PRODUCTION@',production).replace('@COMPLETE@',completion[0])
        cpp, exe = run/(name+'.cpp'),run/(name+'.exe')
        cpp.write_text(harness,encoding='utf8')
        command = ['-O1','-std=c++17','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(lv),'-o',str(exe),str(cpp)]+list(map(str,objects))
        rsp = run/(name+'.rsp')
        rsp.write_text('\n'.join('"'+p.replace('\\','/')+'"' for p in command))
        for stage,cmd in [('compile',[str(args.compiler.resolve()),'@'+str(rsp)]),('execute',[str(exe)])]:
            result = subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=100)
            (run/(name+'-'+stage+'.stdout.log')).write_bytes(result.stdout)
            (run/(name+'-'+stage+'.stderr.log')).write_bytes(result.stderr)
            if result.returncode:
                print(result.stderr.decode(errors='replace')[-8000:]);raise RuntimeError(name+' '+stage+' failed')
        results[name] = json.loads(result.stdout.decode().strip().splitlines()[-1])
    assert results['updated']['max_submit_wait_ms'] == 0
    assert results['updated']['max_completion_wait_ms'] <= 19
    assert results['updated']['mean_added_wait_ms'] < results['baseline']['mean_added_wait_ms']
    evidence = dict(passed=True,results=results,source_sha256=hashlib.sha256(source_file.read_bytes()).hexdigest(),
        method='Real local LVGL timers with byte-identical submission, launch and completion scheduling source; controlled fake worker; 1ms synthetic GUI pump; 100 release phases x 9 worker durations per version.',
        limits='Does not time KDF, hardware frame rendering, flash or preemption; actual GUI pump scheduling is additional. Core crypto/format/limits unchanged.',device_accessed=False)
    (run/'result.json').write_text(json.dumps(evidence,indent=2),encoding='utf8')
    print(json.dumps(evidence))


if __name__ == '__main__':
    main()
