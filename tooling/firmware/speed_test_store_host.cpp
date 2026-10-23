#include "core/speed_test_store.hpp"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <iostream>
using namespace core::speed;
namespace fs=std::filesystem;
Record fixture() {
    Record r;std::strcpy(r.time,"2026-10-13 12:30:01 +0800");std::strcpy(r.ssid,"测试 Wi-Fi / ..");
    std::strcpy(r.ip,"192.168.1.3");std::strcpy(r.bssid,"01:02:03:04:05:06");std::strcpy(r.serverTrace,"test-FRA");
    r.epoch=1791865801;r.durationUs=4000000;r.rssiStart=-48;r.rssiEnd=-50;r.channel=6;r.transferredBytes=1064000;
    r.downloadCount=r.uploadCount=1;r.latencyCount=1;
    Sample s;s.requested=500000;s.bytes=s.acknowledged=500000;s.elapsedUs=1000000;s.headersUs=100000;s.complete=true;s.httpStatus=200;
    r.download[0]=r.upload[0]=s;s.bytes=s.acknowledged=s.requested=0;r.latency[0]=s;return r;
}
std::string name(unsigned n){char s[64];std::snprintf(s,sizeof(s),"20261013-123001-%016x.json",n);return s;}
int main(int argc,char **argv) {
    assert(argc==2);const fs::path root=fs::absolute(argv[1]);assert(!fs::exists(root));fs::create_directories(root);
    const auto r=fixture();Record back;const auto json=store::encode(r);
    assert(store::decode(json,back) && !std::strcmp(r.ssid,back.ssid) && back.upload[0].acknowledged==500000);
    auto legacy=json;const auto warm=legacy.find(",\"warmup\":[]");assert(warm!=std::string::npos);legacy.erase(warm,12);
    assert(store::decode(legacy,back) && back.warmupCount==0);
    auto quality=r;quality.qualityVersion=1;quality.rssiMin=-60;quality.rssiMax=-40;quality.rssiCount=20;
    quality.directionUs={12000000,13000000};quality.trafficLimited[1]=true;
    for(unsigned n=0;n<6;++n) {
        auto &run=quality.probes[n];std::strcpy(run.target,n%2?"1.1.1.1":"192.168.1.1");run.count=ProbeCapacity;
        for(unsigned i=0;i<run.count;++i)run.samples[i]={i%5?int32_t(i*1000):-1,-50};
    }
    auto qualityJson=store::encode(quality);
    assert(qualityJson.size()<store::MaxRecordBytes && store::decode(qualityJson,back));
    assert(back.qualityVersion==1 && back.probes[5].count==ProbeCapacity && back.probes[5].samples[0].elapsedUs==-1 && back.trafficLimited[1]);
    assert(store::encode(back)==qualityJson);
    auto malformed=qualityJson;const auto index=malformed.find("\"rtt_us\":-1");assert(index!=std::string::npos);malformed.replace(index,11,"\"rtt_us\":-2");
    assert(!store::decode(malformed,back));
    auto failedWarm=r;failedWarm.warmupCount=1;failedWarm.warmup[0].error=Error::Connect;failedWarm.warmup[0].nativeError=0x7002;
    assert(store::decode(store::encode(failedWarm),back) && back.warmupCount==1 && back.warmup[0].nativeError==0x7002);
    assert(!store::decode(json+"garbage",back));assert(!store::decode(json.substr(0,json.size()-3),back));
    assert(!store::decode(std::string(store::MaxRecordBytes+1,' '),back));
    auto invalid=json;invalid.replace(invalid.find("\"schema\":1"),10,"\"schema\":9");assert(!store::decode(invalid,back));
    Record wrong=r;wrong.upload[0].acknowledged=7;assert(!store::decode(store::encode(wrong),back));
    wrong=r;wrong.download[0].bytes=100;assert(!store::decode(store::encode(wrong),back));
    wrong=r;wrong.latency[0].bytes=8192;wrong.latency[0].complete=false;wrong.latency[0].error=Error::Length;
    assert(store::decode(store::encode(wrong),back) && back.latency[0].bytes==8192 && !back.latency[0].complete);
    wrong=r;wrong.download[0].bytes=MaxBytes+8192;wrong.download[0].complete=false;wrong.download[0].error=Error::Length;
    assert(store::decode(store::encode(wrong),back) && throughput(back.download,back.downloadCount).valid==0);
    assert(!store::validFilename("../history.json") && !store::validFilename("20261013-123001-0123456789ABCDEF.json"));
    assert(store::save(root.string(),"../escape.json",r)==EINVAL);
    assert(store::read(root.string(),"../escape.json",back)==EINVAL);
    assert(store::save((root/"missing").string(),name(0),r)==ENOENT);
    for(unsigned i=0;i<23;++i)assert(!store::save(root.string(),name(i),r));
    assert(store::save(root.string(),name(0),r)==EEXIST);
    assert(!store::read(root.string(),name(0),back) && !std::strcmp(back.ssid,r.ssid));
    std::ofstream(root/(name(24)+".tmp"))<<"unfinished";
    std::ofstream(root/name(25))<<"{broken";
    // A cancellation after flush must remove the temp and leave no final file.
    unsigned checks=0;
    store::Hooks hooks{&checks,[](void *p){return ++*static_cast<unsigned *>(p)>=2;}};
    assert(store::save(root.string(),name(26),r,hooks)==ECANCELED);
    assert(!fs::exists(root/name(26)) && !fs::exists(root/(name(26)+".tmp")));
    std::set<std::string> seen;std::string before;unsigned skipped=0,pages=0;
    for(;;) {
        auto page=store::list(root.string(),before);assert(!page.error && page.entries.size()<=PageSize);
        ++pages;skipped+=page.skipped;std::string previous=before;
        for(const auto &entry:page.entries){assert(previous.empty() || entry.filename<previous);previous=entry.filename;assert(seen.insert(entry.filename).second);}
        if(!page.more)break;assert(!page.cursor.empty() && page.cursor!=before);before=page.cursor;
    }
    assert(seen.size()==23 && skipped==1 && pages==3);
    auto cancelled=store::list(root.string(),{}, {nullptr,[](void *){return true;}});assert(cancelled.error==ECANCELED);
    assert(store::list(root.string(),"../../").error==EINVAL);
    assert(!fs::exists(root.parent_path()/"escape.json"));
    // Delete only the selected unchanged regular file; preserve neighbours.
    store::Entry selected{name(0),r};
    assert(store::erase(root.string(),{"../escape.json",r})==EINVAL);
    assert(store::erase(root.string(),selected,{nullptr,[](void *){return true;}})==ECANCELED);
    assert(fs::exists(root/name(0)));
    checks=0;assert(store::erase(root.string(),selected,hooks)==ECANCELED && fs::exists(root/name(0)));
    auto stale=selected;stale.record.rssiEnd=-70;
    assert(store::erase(root.string(),stale)==ESTALE && fs::exists(root/name(0)));
    assert(!store::erase(root.string(),selected) && !fs::exists(root/name(0)));
    assert(store::erase(root.string(),selected)==ENOENT);
    assert(!store::read(root.string(),name(1),back) && store::encode(back)==json);
    const auto refreshed=store::list(root.string());
    for(const auto &entry:refreshed.entries)assert(entry.filename!=name(0));
    fs::create_directory(root/name(30));std::ofstream(root/name(30)/"keep.txt")<<"preserved";
    assert(store::erase(root.string(),{name(30),r})==EINVAL && fs::exists(root/name(30)/"keep.txt"));
    assert(fs::exists(root/(name(24)+".tmp")) && fs::exists(root/name(25)));
    std::cout<<"Speed history: JSON roundtrip, malformed records, byte acknowledgements, safe paths, no overwrite, cancelled commit and 3-page traversal passed\n";
}
