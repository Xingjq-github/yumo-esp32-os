#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace core::speed {
inline constexpr unsigned Version=1, LatencyCount=10, TransferCount=16, PageSize=8;
inline constexpr uint32_t FirstBytes=16000, MinBytes=16000, MaxBytes=4000000;
inline constexpr int64_t RequestLimitUs=15000000, RunLimitUs=120000000;
inline constexpr int64_t TransferTargetUs=12000000, TransferMinimumUs=10000000, DirectionLimitUs=25000000;
inline constexpr uint64_t TrafficLimitBytes=32000000;
inline constexpr unsigned ProbeCapacity=48, IdleProbeCount=20;
inline constexpr char InternetProbeTarget[]="1.1.1.1";
inline constexpr char Host[]="speed.cloudflare.com";
inline constexpr char DownloadUrl[]="https://speed.cloudflare.com/__down";
inline constexpr char UploadUrl[]="https://speed.cloudflare.com/__up";
enum class Error : uint8_t { None, Cancelled, Wifi, Clock, Memory, Connect, Http, Timeout, Length, Encoding, NetworkChanged, Write, Headers, Read, DownloadLength, UploadAck, ResponseLength, Timing };
enum class Phase : uint8_t { Preparing, Latency, Download, Upload, Saving, Finished };
struct Sample {
    uint32_t requested=0, acknowledged=0;
    uint64_t bytes=0;
    int64_t elapsedUs=0, headersUs=0, connectUs=0;
    int httpStatus=0, nativeError=0;
    Error error=Error::None;
    bool complete=false, reused=false;
};
struct ProbeSample {int32_t elapsedUs=-1;int16_t rssi=-127;};
struct ProbeRun {
    char target[16]{};
    std::array<ProbeSample,ProbeCapacity> samples{};
    unsigned count=0;
    int error=0;
};
// [idle gateway, idle internet, download gateway, download internet, upload gateway, upload internet]
struct Record {
    char time[40]{}, ssid[33]{}, ip[16]{}, bssid[18]{}, serverTrace[96]{};
    int64_t epoch=0, durationUs=0;
    int rssiStart=0, rssiEnd=0, channel=0;
    uint64_t transferredBytes=0; // Includes warm-up and partial payloads, excludes protocol headers.
    std::array<Sample,LatencyCount> latency{};
    std::array<Sample,TransferCount> download{},upload{};
    std::array<Sample,2> warmup{};
    unsigned warmupCount=0;
    unsigned latencyCount=0, downloadCount=0, uploadCount=0;
    Error error=Error::None;
    bool clockSynced=false;
    unsigned qualityVersion=0; // absent in legacy schema-1 records
    std::array<ProbeRun,6> probes{};
    std::array<int64_t,2> directionUs{};
    std::array<bool,2> trafficLimited{};
    int rssiMin=-127,rssiMax=-127;
    unsigned rssiCount=0;
};
// Decimal SI: Mbps = payload bytes * 8 / elapsed microseconds.
constexpr double mbps(uint64_t bytes,int64_t us) {return us>0?double(bytes)*8.0/double(us):0.0;}
struct Throughput {
    unsigned valid=0, attempted=0;
    uint64_t bytes=0;
    int64_t elapsedUs=0;
    double average=0, best=0, worst=0;
};
inline Throughput throughput(const std::array<Sample,TransferCount> &samples,unsigned count) {
    Throughput out;out.attempted=std::min(count,TransferCount);
    for(unsigned i=0;i<out.attempted;++i) {
        const auto &s=samples[i];
        if(!s.complete || s.error!=Error::None || s.httpStatus!=200 || s.elapsedUs<=0 || s.bytes!=s.requested || !s.bytes)continue;
        const double rate=mbps(s.bytes,s.elapsedUs);
        if(!out.valid || rate<out.worst)out.worst=rate;
        out.best=std::max(out.best,rate);++out.valid;out.bytes+=s.bytes;out.elapsedUs+=s.elapsedUs;
    }
    out.average=mbps(out.bytes,out.elapsedUs);return out;
}
struct Latency {
    unsigned valid=0, attempted=0, jitterPairs=0;
    double minimum=0, maximum=0, mean=0, median=0, p95=0, jitter=0;
};
inline Latency latency(const Record &record) {
    Latency out;out.attempted=std::min(record.latencyCount,LatencyCount);
    std::array<double,LatencyCount> values{};
    bool previousValid=false;double previous=0;
    for(unsigned i=0;i<out.attempted;++i) {
        const auto &s=record.latency[i];
        const bool valid=s.complete && s.error==Error::None && s.httpStatus==200 && s.headersUs>0;
        if(!valid){previousValid=false;continue;}
        const double ms=double(s.headersUs)/1000;
        values[out.valid++]=ms;out.mean+=ms;
        if(previousValid){out.jitter+=ms>previous?ms-previous:previous-ms;++out.jitterPairs;}
        previous=ms;previousValid=true;
    }
    if(!out.valid)return out;
    std::sort(values.begin(),values.begin()+out.valid);
    out.minimum=values[0];out.maximum=values[out.valid-1];out.mean/=out.valid;
    out.median=out.valid%2?values[out.valid/2]:(values[out.valid/2-1]+values[out.valid/2])/2;
    out.p95=values[(95*out.valid+99)/100-1]; // nearest-rank percentile
    if(out.jitterPairs)out.jitter/=out.jitterPairs;
    return out;
}
constexpr uint32_t nextBytes(const Sample &sample) {
    if(!sample.complete || sample.elapsedUs<=0)
        return sample.requested>MinBytes?std::max(MinBytes,sample.requested/2):FirstBytes;
    // Aim for two seconds per next request, bounded in bytes and wall time.
    const uint64_t estimate=sample.bytes*2000000/uint64_t(sample.elapsedUs);
    return estimate<MinBytes?MinBytes:estimate>MaxBytes?MaxBytes:uint32_t(estimate);
}
inline Latency probeStats(const ProbeRun &run) {
    Latency out;out.attempted=std::min(run.count,ProbeCapacity);
    std::array<double,ProbeCapacity> values{};
    bool previousValid=false;double previous=0;
    for(unsigned i=0;i<out.attempted;++i) {
        const auto us=run.samples[i].elapsedUs;
        if(us<0){previousValid=false;continue;}
        const double ms=double(us)/1000;values[out.valid++]=ms;out.mean+=ms;
        if(previousValid){out.jitter+=std::abs(ms-previous);++out.jitterPairs;}
        previous=ms;previousValid=true;
    }
    if(!out.valid)return out;
    std::sort(values.begin(),values.begin()+out.valid);
    out.minimum=values[0];out.maximum=values[out.valid-1];out.mean/=out.valid;
    out.median=out.valid%2?values[out.valid/2]:(values[out.valid/2-1]+values[out.valid/2])/2;
    out.p95=values[(95*out.valid+99)/100-1];
    if(out.jitterPairs)out.jitter/=out.jitterPairs;
    return out;
}
enum class Confidence {Unavailable,Limited,Good};
inline Confidence confidence(const Throughput &t) {
    if(!t.valid)return Confidence::Unavailable;
    return t.valid>=3 && t.elapsedUs>=TransferMinimumUs && t.valid*10>=t.attempted*9?Confidence::Good:Confidence::Limited;
}
inline bool transferSatisfied(const std::array<Sample,TransferCount> &samples,unsigned count) {
    const auto t=throughput(samples,count);return t.valid>=3 && t.elapsedUs>=TransferTargetUs;
}
inline bool complete(const Record &r) {
    if(r.qualityVersion) return r.error==Error::None && confidence(throughput(r.download,r.downloadCount))==Confidence::Good &&
        confidence(throughput(r.upload,r.uploadCount))==Confidence::Good && latency(r).valid>=8;
    return r.error==Error::None && latency(r).valid==LatencyCount &&
        throughput(r.download,r.downloadCount).valid==4 && throughput(r.upload,r.uploadCount).valid==4;
}
inline const char *errorText(Error error) {
    switch(error) {
    case Error::None:return "完成";
    case Error::Cancelled:return "已取消";
    case Error::Wifi:return "Wi-Fi未连接";
    case Error::Clock:return "请先校准设备时间";
    case Error::Memory:return "内存不足";
    case Error::Connect:return "连接或证书校验失败";
    case Error::Http:return "服务器响应异常";
    case Error::Timeout:return "请求超时";
    case Error::Length:return "数据传输不完整";
    case Error::Encoding:return "服务器返回压缩数据";
    case Error::NetworkChanged:return "网络已切换或断开";
    case Error::Write:return "上传发送中断";
    case Error::Headers:return "响应头接收失败";
    case Error::Read:return "响应读取失败";
    case Error::DownloadLength:return "下载数据未收齐";
    case Error::UploadAck:return "服务器未完整确认上传";
    case Error::ResponseLength:return "响应未完整接收";
    case Error::Timing:return "请求计时无效";
    }
    return "测试失败";
}
} // namespace core::speed
