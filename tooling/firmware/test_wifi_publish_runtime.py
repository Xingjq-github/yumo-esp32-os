"""Replay production Wi-Fi snapshot publication with allocation fault injection."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <tuple>
static int fail_after=-1;
static unsigned allocations=0, copies=0, checks=0;
void *operator new(size_t size) {
    if(fail_after==0) throw std::bad_alloc();
    if(fail_after>0)--fail_after;
    ++allocations;
    if(void *p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete(void *p,size_t) noexcept {std::free(p);}
static void check(bool value,int line) {++checks;if(!value){std::fprintf(stderr,"line %d\n",line);std::_Exit(1);}}
#define CHECK(x) check((x),__LINE__)
using esp_err_t=int;
constexpr int ESP_OK=0;
struct wifi_ap_record_t {
    int marker=0;
    wifi_ap_record_t &operator=(const wifi_ap_record_t &other){++copies;marker=other.marker;return *this;}
};
#include "core/connectivity.hpp"
namespace sys {
std::mutex viewMutex;
WifiSnapshot state,view;
bool transitionTimedOut=false,awaitingStart=false,awaitingStop=false,restartPending=false,ignoreDisconnect=false,scanOutstanding=false;
int64_t retryAt=0;
wifi_ap_record_t source[24],published[24];
wifi_ap_record_t *networks=source,*viewNetworks=published;
uint16_t networkCount=0,viewCount=0;
@PUBLISH@
}
static auto fields(const sys::WifiSnapshot &v) {
    return std::tie(v.enabled,v.transitioning,v.state,v.scan,v.phase,v.ssid,v.ip,v.rssi,v.error,v.scanError,v.retry,v.retrying,v.scanPending,v.revision,v.scanRevision);
}
static void publish(){sys::PublishView guard;}
int main() {
    // The old destructor terminates instead of letting the service retry.
    std::set_terminate([](){std::_Exit(86);});
    using namespace sys;
    state.enabled=true;state.state=WifiState::Connected;state.scan=WifiScanState::Complete;
    state.phase=WifiConnectPhase::Online;state.ssid=std::string(32,'s');state.ip="192.168.100.100";
    state.rssi=-40;state.error=7;state.scanError=8;state.retry=2;state.revision=5;state.scanRevision=9;
    networkCount=24;for(int i=0;i<24;++i)source[i].marker=i+1;
    const auto empty=view;
    fail_after=0;publish();fail_after=-1;
    CHECK(fields(view)==fields(empty));CHECK(viewCount==0&&copies==0);
    // A failing publish while another exception unwinds must not terminate.
    try {fail_after=0;PublishView guard;throw 7;}catch(int n){fail_after=-1;CHECK(n==7);}
    CHECK(fields(view)==fields(empty));
    publish();CHECK(fields(view)==fields(state));CHECK(viewCount==24&&copies==24);
    for(int i=0;i<24;++i)CHECK(published[i].marker==i+1);
    const auto before=allocations;copies=0;
    for(unsigned i=0;i<1200;++i)publish();
    CHECK(allocations==before);CHECK(copies==0);CHECK(fields(view)==fields(state));
    // Transition flags/RSSI can change without the scan revision changing.
    awaitingStart=true;scanOutstanding=true;retryAt=1;state.rssi=-70;publish();
    CHECK(view.transitioning&&view.scanPending&&view.retrying&&view.rssi==-70&&copies==0);
    transitionTimedOut=true;publish();CHECK(!view.transitioning&&view.scanPending&&view.retrying);
    awaitingStart=false;scanOutstanding=false;retryAt=0;publish();CHECK(!view.scanPending&&!view.retrying);
    for(int i=0;i<24;++i)source[i].marker=100+i;
    ++state.scanRevision;publish();CHECK(copies==24);
    for(int i=0;i<24;++i)CHECK(published[i].marker==100+i);
    networkCount=0;publish();CHECK(viewCount==0&&copies==24);
    networkCount=24;publish();CHECK(viewCount==24&&copies==48);
    // Exercise a second reserve failure after the first reserve succeeded.
    // Oversized strings are synthetic solely to force both allocations.
    const auto previous=view;
    state.ssid=std::string(view.ssid.capacity()+1,'x');
    state.ip=std::string(view.ip.capacity()+1,'y');
    state.error=99;++state.scanRevision;source[0].marker=700;
    fail_after=1;publish();fail_after=-1;
    CHECK(fields(view)==fields(previous));CHECK(published[0].marker==100&&copies==48);
    publish();CHECK(view.ssid==state.ssid&&view.ip==state.ip&&view.error==99);
    CHECK(published[0].marker==700&&copies==72);
    std::printf("{\"passed\":true,\"checks\":%u,\"idle_polls\":1200,\"idle_record_copies\":0,\"idle_allocations\":0,\"allocation_fault_cases\":3}\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True, help='External host g++ compiler path')
    parser.add_argument('--source', type=Path, default=ROOT/'firmware/main/core/connectivity.cpp')
    parser.add_argument('--expect-terminate', action='store_true')
    args = parser.parse_args()
    run = args.run_dir.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    source = args.source.resolve()
    raw = source.read_text(encoding='utf8')
    start = raw.index('struct PublishView {')
    body = raw[start:raw.index('\n};', start)+3]
    (run/'replay.cpp').write_text(HARNESS.replace('@PUBLISH@', body), encoding='utf8')
    for name in ('esp_err.h', 'esp_wifi_types.h'):
        (run/name).write_text('#pragma once\n', encoding='utf8')
    compiler = args.compiler.resolve()
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent)+os.pathsep+env.get('PATH', '')
    exe = run/'replay.exe'
    command = [str(compiler), '-std=c++17', '-O1', '-I'+str(run), '-I'+str(ROOT/'firmware/main'), str(run/'replay.cpp'), '-o', str(exe)]
    built = subprocess.run(command, env=env, capture_output=True, timeout=90)
    (run/'compile.log').write_bytes(built.stdout+built.stderr)
    built.check_returncode()
    replay = subprocess.run([str(exe)], env=env, capture_output=True, timeout=20)
    (run/'execute.log').write_bytes(replay.stdout+replay.stderr)
    expected = 86 if args.expect_terminate else 0
    report = dict(passed=replay.returncode == expected, expected_exit=expected, actual_exit=replay.returncode,
                  source=str(source.relative_to(ROOT)), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  limits='Production scope guard and public snapshot fields; synthetic allocation failure and counted AP assignments; no radio/device timing')
    if replay.returncode == 0:
        report['replay'] = json.loads(replay.stdout.decode())
    (run/'result.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(report))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
