#include "core/speed_test_model.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace core::speed;
static_assert(mbps(1000000,1000000)==8.0);
static_assert(mbps(1000000,0)==0.0);
static_assert(mbps(1000000,-1)==0.0);
constexpr Sample tiny(){Sample s;s.complete=true;s.bytes=1;s.elapsedUs=10000000;return s;}
constexpr Sample fast(){Sample s;s.complete=true;s.bytes=MaxBytes;s.elapsedUs=1;return s;}
static_assert(nextBytes(tiny())==MinBytes);
static_assert(nextBytes(fast())==MaxBytes);
static_assert(nextBytes(Sample{})==FirstBytes);
Sample good(uint32_t bytes,int64_t us) {
    Sample s;s.requested=bytes;s.bytes=bytes;s.acknowledged=bytes;s.elapsedUs=us;s.headersUs=us;
    s.complete=true;s.httpStatus=200;return s;
}
int main() {
    std::array<Sample,TransferCount> samples{};
    samples[0]=good(1000000,1000000);samples[1]=good(1000000,3000000);
    samples[2]=good(4000000,1);samples[2].error=Error::Length;samples[2].complete=false;
    auto rate=throughput(samples,3);
    assert(rate.valid==2 && rate.attempted==3 && rate.bytes==2000000 && rate.elapsedUs==4000000);
    assert(rate.average==4.0 && rate.best==8.0 && std::abs(rate.worst-8.0/3)<1e-9);
    samples[0].httpStatus=503;samples[1].bytes=999999;
    assert(throughput(samples,4).valid==0);
    Record record;const int times[]={10,20,30,40,50,60,70,80,90,100};
    for(unsigned n=0;n<LatencyCount;++n)record.latency[n]=good(0,times[n]*1000);
    record.latencyCount=LatencyCount;auto v=latency(record);
    assert(v.valid==10 && v.minimum==10 && v.maximum==100 && v.mean==55 && v.median==55 && v.p95==100);
    assert(v.jitter==10 && v.jitterPairs==9);
    record.latency[1].complete=false;record.latency[1].error=Error::Timeout;
    v=latency(record);assert(v.valid==9 && v.jitterPairs==7 && v.jitter==10 && v.median==60);
    record={};record.latencyCount=1;record.latency[0]=good(0,1000);v=latency(record);
    assert(v.valid==1 && v.median==1 && !v.jitterPairs);
    record.latency[0].headersUs=0;assert(!latency(record).valid);
    // Every expected sample must succeed; failure must not look like 0 Mbps.
    record={};record.latencyCount=LatencyCount;record.downloadCount=record.uploadCount=4;
    for(auto &s:record.latency)s=good(0,100000);
    for(auto &s:record.download)s=good(1000000,1000000);
    for(auto &s:record.upload)s=good(1000000,1000000);
    assert(complete(record));record.error=Error::Cancelled;assert(!complete(record));
    // The real slow-link trace timed out at 204800/256000 bytes after 15.7 s.
    // Start conservatively, size downloads from warm-up and back off failures.
    assert(FirstBytes==16000 && MinBytes==16000);
    auto slow=good(64000,4303041);const auto adapted=nextBytes(slow);
    assert(adapted>=MinBytes && adapted<64000);
    slow=good(256000,15677883);slow.bytes=204800;slow.complete=false;slow.error=Error::Timeout;
    assert(nextBytes(slow)==128000 && !throughput({slow},1).valid);
    slow.requested=MinBytes;assert(nextBytes(slow)==MinBytes);
    for(unsigned bytes=64000;bytes<=MaxBytes;bytes+=31777)for(int64_t us=1;us<15000000;us=us*3+1) {
        const auto s=good(bytes,us);const auto next=nextBytes(s);
        assert(next>=MinBytes && next<=MaxBytes && mbps(bytes,us)>0);
    }
    ProbeRun probes;probes.count=6;
    const int32_t delays[]={0,10000,-1,30000,40000,50000};
    for(unsigned i=0;i<6;++i)probes.samples[i].elapsedUs=delays[i];
    const auto pv=probeStats(probes);
    assert(pv.valid==5 && pv.attempted==6 && pv.median==30 && pv.p95==50 && pv.jitterPairs==3 && pv.jitter==10);
    probes={};probes.count=20;assert(!probeStats(probes).valid);
    assert(confidence(Throughput{})==Confidence::Unavailable);
    auto t=throughput({good(1000000,4000000),good(1000000,4000000),good(1000000,4000000)},3);
    assert(confidence(t)==Confidence::Good);
    assert(transferSatisfied({good(1000000,4000000),good(1000000,4000000),good(1000000,4000000)},3));
    t.attempted=4;assert(confidence(t)==Confidence::Limited);
    t.attempted=3;t.elapsedUs=9999999;assert(confidence(t)==Confidence::Limited);
    record={};record.qualityVersion=1;record.latencyCount=10;record.downloadCount=record.uploadCount=3;
    for(auto &s:record.latency)s=good(0,100000);
    for(auto &s:record.download)s=good(1000000,4000000);
    for(auto &s:record.upload)s=good(1000000,4000000);
    assert(complete(record));record.download[1].complete=false;assert(!complete(record));
    std::puts("Speed model: SI units, weighted rates, invalid samples, percentiles, jitter gaps and adaptive bounds passed");
}
