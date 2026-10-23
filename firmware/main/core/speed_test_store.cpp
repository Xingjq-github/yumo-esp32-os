#include "speed_test_store.hpp"
#include "cJSON.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif

namespace core::speed::store {
namespace {
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
using File=std::unique_ptr<FILE,decltype(&std::fclose)>;
bool stopped(Hooks h){return h.cancelled && h.cancelled(h.context);}
int ioError(){return errno?errno:EIO;}
int syncFile(FILE *file){
    if(std::fflush(file)!=0)return -1;
#ifdef _WIN32
    return _commit(_fileno(file));
#else
    return fsync(fileno(file));
#endif
}
void number(cJSON *object,const char *key,double value,bool &ok){if(!cJSON_AddNumberToObject(object,key,value))ok=false;}
void text(cJSON *object,const char *key,const char *value,bool &ok){if(!cJSON_AddStringToObject(object,key,value))ok=false;}
void boolean(cJSON *object,const char *key,bool value,bool &ok){if(!cJSON_AddBoolToObject(object,key,value))ok=false;}
template<size_t N> void samples(cJSON *root,const char *key,const std::array<Sample,N> &values,unsigned count,bool &ok) {
    auto list=cJSON_AddArrayToObject(root,key);if(!list){ok=false;return;}
    for(unsigned i=0;i<std::min<unsigned>(count,N);++i) {
        auto item=cJSON_CreateObject();if(!item){ok=false;return;}
        if(!cJSON_AddItemToArray(list,item)){cJSON_Delete(item);ok=false;return;}
        const auto &s=values[i];
        number(item,"requested_bytes",s.requested,ok);number(item,"payload_bytes",s.bytes,ok);number(item,"acknowledged_bytes",s.acknowledged,ok);
        number(item,"elapsed_us",s.elapsedUs,ok);number(item,"headers_us",s.headersUs,ok);number(item,"connect_us",s.connectUs,ok);
        number(item,"http_status",s.httpStatus,ok);number(item,"native_error",s.nativeError,ok);number(item,"error",int(s.error),ok);
        boolean(item,"complete",s.complete,ok);boolean(item,"reused_connection",s.reused,ok);
    }
}
bool num(const cJSON *o,const char *key,double lo,double hi,double &out) {
    const auto p=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) || p->valuedouble<lo || p->valuedouble>hi || std::floor(p->valuedouble)!=p->valuedouble)return false;
    out=p->valuedouble;return true;
}
template<class T> bool integer(const cJSON *o,const char *key,double lo,double hi,T &out) {
    double n;if(!num(o,key,lo,hi,n))return false;out=static_cast<T>(n);return true;
}
bool boolean(const cJSON *o,const char *key,bool &out) {
    const auto p=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsBool(p))return false;out=cJSON_IsTrue(p);return true;
}
template<size_t N> bool text(const cJSON *o,const char *key,char (&out)[N]) {
    const auto p=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsString(p) || !p->valuestring || std::strlen(p->valuestring)>=N)return false;
    std::strcpy(out,p->valuestring);return true;
}
template<size_t N> bool samples(const cJSON *root,const char *key,std::array<Sample,N> &values,unsigned &count) {
    const auto list=cJSON_GetObjectItemCaseSensitive(root,key);
    if(!cJSON_IsArray(list) || cJSON_GetArraySize(list)>int(N))return false;
    count=cJSON_GetArraySize(list);
    for(unsigned i=0;i<count;++i) {
        const auto o=cJSON_GetArrayItem(list,i);auto &s=values[i];unsigned error=0;
        if(!cJSON_IsObject(o) || !integer(o,"requested_bytes",0,MaxBytes,s.requested) ||
           !integer(o,"payload_bytes",0,MaxBytes+8192,s.bytes) || !integer(o,"acknowledged_bytes",0,MaxBytes,s.acknowledged) || !integer(o,"elapsed_us",0,RunLimitUs+30000000,s.elapsedUs) ||
           !integer(o,"headers_us",0,RunLimitUs+30000000,s.headersUs) || !integer(o,"connect_us",0,RunLimitUs+30000000,s.connectUs) ||
           !integer(o,"http_status",0,599,s.httpStatus) || !integer(o,"native_error",INT32_MIN,INT32_MAX,s.nativeError) ||
           !integer(o,"error",0,int(Error::Timing),error) || !boolean(o,"complete",s.complete) || !boolean(o,"reused_connection",s.reused))return false;
        s.error=static_cast<Error>(error);
        if(s.complete && (s.error!=Error::None || s.httpStatus!=200 || s.elapsedUs<=0 || s.bytes!=s.requested || s.headersUs<=0))return false;
        if(s.headersUs>s.elapsedUs)return false;
    }
    return true;
}
}
bool validFilename(const std::string &name) {
    if(name.size()!=37 || name[8]!='-' || name[15]!='-' || name.compare(32,5,".json"))return false;
    for(unsigned i=0;i<32;++i) {
        if(i==8 || i==15)continue;
        const char c=name[i];if((c<'0'||c>'9') && (i<16 || c<'a'||c>'f'))return false;
    }
    return true;
}
std::string encode(const Record &r) {
    Json root(cJSON_CreateObject(),cJSON_Delete);if(!root)return {};
    bool ok=true;auto o=root.get();
    number(o,"schema",Version,ok);text(o,"method","yumo-https-payload-v1",ok);
    text(o,"download_url",DownloadUrl,ok);text(o,"upload_url",UploadUrl,ok);
    text(o,"rate_unit","Mbps (1000000 bit/s)",ok);
    text(o,"rate_method","8 * sum(successful payload bytes) / sum(successful request elapsed microseconds); connection setup excluded; server acknowledgement included",ok);
    text(o,"latency_method","HTTP request through complete response headers; connection setup excluded; includes server processing",ok);
    text(o,"jitter_method","mean absolute difference of adjacent successful latency samples; failures break adjacency",ok);
    text(o,"time",r.time,ok);text(o,"ssid",r.ssid,ok);text(o,"ip",r.ip,ok);text(o,"bssid",r.bssid,ok);text(o,"server_trace",r.serverTrace,ok);
    number(o,"epoch",double(r.epoch),ok);number(o,"duration_us",r.durationUs,ok);number(o,"payload_total_bytes",r.transferredBytes,ok);
    number(o,"rssi_start_dbm",r.rssiStart,ok);number(o,"rssi_end_dbm",r.rssiEnd,ok);number(o,"channel",r.channel,ok);
    number(o,"error",int(r.error),ok);boolean(o,"clock_synced",r.clockSynced,ok);
    samples(o,"latency",r.latency,r.latencyCount,ok);samples(o,"download",r.download,r.downloadCount,ok);samples(o,"upload",r.upload,r.uploadCount,ok);
    samples(o,"warmup",r.warmup,r.warmupCount,ok);
    if(r.qualityVersion) {
        number(o,"quality_version",r.qualityVersion,ok);
        auto quality=cJSON_AddObjectToObject(o,"quality");if(!quality)ok=false;
        if(quality) {
            number(quality,"rssi_min_dbm",r.rssiMin,ok);number(quality,"rssi_max_dbm",r.rssiMax,ok);number(quality,"rssi_samples",r.rssiCount,ok);
            number(quality,"download_duration_us",r.directionUs[0],ok);number(quality,"upload_duration_us",r.directionUs[1],ok);
            boolean(quality,"download_traffic_limited",r.trafficLimited[0],ok);boolean(quality,"upload_traffic_limited",r.trafficLimited[1],ok);
            text(quality,"probe_method","ICMP echo: 32-byte payload; 500 ms interval; 800 ms timeout; monotonic RTT; no reply is not proof of packet loss",ok);
            auto runs=cJSON_AddArrayToObject(quality,"probes");if(!runs)ok=false;
            for(const auto &run:r.probes) {
                auto item=cJSON_CreateObject();if(!item){ok=false;break;}
                if(!runs || !cJSON_AddItemToArray(runs,item)){cJSON_Delete(item);ok=false;break;}
                text(item,"target",run.target,ok);number(item,"error",run.error,ok);
                auto list=cJSON_AddArrayToObject(item,"samples");if(!list){ok=false;break;}
                for(unsigned i=0;i<std::min(run.count,ProbeCapacity);++i) {
                    auto sample=cJSON_CreateObject();if(!sample){ok=false;break;}
                    if(!cJSON_AddItemToArray(list,sample)){cJSON_Delete(sample);ok=false;break;}
                    number(sample,"rtt_us",run.samples[i].elapsedUs,ok);number(sample,"rssi_dbm",run.samples[i].rssi,ok);
                }
            }
        }
    }
    if(!ok)return {};
    std::unique_ptr<char,decltype(&cJSON_free)> data(cJSON_PrintUnformatted(o),cJSON_free);
    return data?std::string(data.get()):std::string{};
}
bool decode(const std::string &json,Record &record) {
    if(json.empty() || json.size()>MaxRecordBytes || json.find('\0')!=std::string::npos)return false;
    Json root(cJSON_ParseWithLengthOpts(json.c_str(),json.size()+1,nullptr,true),cJSON_Delete);if(!root)return false;
    auto o=root.get();double schema=0;Record r;unsigned error=0;
    const auto method=cJSON_GetObjectItemCaseSensitive(o,"method");
    if(!num(o,"schema",Version,Version,schema) || !cJSON_IsString(method) || std::strcmp(method->valuestring,"yumo-https-payload-v1"))return false;
    if(!text(o,"time",r.time) || !text(o,"ssid",r.ssid) || !text(o,"ip",r.ip) || !text(o,"bssid",r.bssid) || !text(o,"server_trace",r.serverTrace) ||
       !integer(o,"epoch",0,4102444800.0,r.epoch) || !integer(o,"duration_us",0,RunLimitUs+60000000,r.durationUs) ||
       !integer(o,"payload_total_bytes",0,2.0*TransferCount*MaxBytes+1000000,r.transferredBytes) ||
       !integer(o,"rssi_start_dbm",-127,0,r.rssiStart) || !integer(o,"rssi_end_dbm",-127,0,r.rssiEnd) || !integer(o,"channel",0,14,r.channel) ||
       !integer(o,"error",0,int(Error::Timing),error) || !boolean(o,"clock_synced",r.clockSynced) ||
       !samples(o,"latency",r.latency,r.latencyCount) || !samples(o,"download",r.download,r.downloadCount) || !samples(o,"upload",r.upload,r.uploadCount))return false;
    // Optional extension: old schema-1 records remain readable.
    if(cJSON_GetObjectItemCaseSensitive(o,"warmup") && !samples(o,"warmup",r.warmup,r.warmupCount))return false;
    for(unsigned i=0;i<r.uploadCount;++i)if(r.upload[i].complete && r.upload[i].acknowledged!=r.upload[i].requested)return false;
    for(unsigned i=0;i<r.latencyCount;++i)if(r.latency[i].requested || (r.latency[i].complete && r.latency[i].bytes))return false;
    if(cJSON_GetObjectItemCaseSensitive(o,"quality_version")) {
        const auto q=cJSON_GetObjectItemCaseSensitive(o,"quality");
        if(!integer(o,"quality_version",1,1,r.qualityVersion) || !cJSON_IsObject(q) ||
           !integer(q,"rssi_min_dbm",-127,0,r.rssiMin) || !integer(q,"rssi_max_dbm",-127,0,r.rssiMax) ||
           !integer(q,"rssi_samples",0,1000,r.rssiCount) || r.rssiMin>r.rssiMax ||
           !integer(q,"download_duration_us",0,RunLimitUs,r.directionUs[0]) || !integer(q,"upload_duration_us",0,RunLimitUs,r.directionUs[1]) ||
           !boolean(q,"download_traffic_limited",r.trafficLimited[0]) || !boolean(q,"upload_traffic_limited",r.trafficLimited[1]) ||
           r.transferredBytes>TrafficLimitBytes)return false;
        const auto runs=cJSON_GetObjectItemCaseSensitive(q,"probes");
        if(!cJSON_IsArray(runs) || cJSON_GetArraySize(runs)!=6)return false;
        for(unsigned n=0;n<6;++n) {
            const auto item=cJSON_GetArrayItem(runs,n);auto &run=r.probes[n];
            if(!cJSON_IsObject(item) || !text(item,"target",run.target) || !integer(item,"error",INT32_MIN,INT32_MAX,run.error))return false;
            const auto list=cJSON_GetObjectItemCaseSensitive(item,"samples");
            if(!cJSON_IsArray(list) || cJSON_GetArraySize(list)>int(ProbeCapacity))return false;
            run.count=cJSON_GetArraySize(list);
            for(unsigned i=0;i<run.count;++i) {
                const auto sample=cJSON_GetArrayItem(list,i);
                if(!cJSON_IsObject(sample) || !integer(sample,"rtt_us",-1,1000000,run.samples[i].elapsedUs) ||
                   !integer(sample,"rssi_dbm",-127,0,run.samples[i].rssi))return false;
            }
        }
    }
    r.error=static_cast<Error>(error);record=r;return true;
}
int save(const std::string &directory,const std::string &filename,const Record &record,Hooks hooks) {
    if(!validFilename(filename))return EINVAL;
    if(stopped(hooks))return ECANCELED;
    const std::string data=encode(record);
    if(data.empty() || data.size()>MaxRecordBytes)return ENOMEM;
    const auto path=directory+"/"+filename,tmp=path+".tmp";
    struct stat st{};
    if(stat(path.c_str(),&st)==0)return EEXIST;
    if(errno!=ENOENT)return ioError();
    const int fd=open(tmp.c_str(),O_CREAT|O_EXCL|O_WRONLY,0644);
    if(fd<0)return ioError();
    File file(fdopen(fd,"wb"),std::fclose);
    if(!file){const int error=ioError();close(fd);std::remove(tmp.c_str());return error;}
    int error=0;
    if(std::fwrite(data.data(),1,data.size(),file.get())!=data.size() || syncFile(file.get())!=0)error=ioError();
    if(std::fclose(file.release())!=0 && !error)error=ioError();
    if(!error && stopped(hooks))error=ECANCELED;
    // The device's exclusive SD mutation claim spans this check and rename.
    if(!error) {
        if(stat(path.c_str(),&st)==0)error=EEXIST;
        else if(errno!=ENOENT)error=ioError();
    }
    if(!error && std::rename(tmp.c_str(),path.c_str())!=0)error=ioError();
    if(error)std::remove(tmp.c_str());
    return error;
}
int read(const std::string &directory,const std::string &filename,Record &record) {
    if(!validFilename(filename))return EINVAL;
    const auto path=directory+"/"+filename;struct stat st{};
    if(stat(path.c_str(),&st)!=0)return ioError();
    if(!S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>int64_t(MaxRecordBytes))return EINVAL;
    File file(std::fopen(path.c_str(),"rb"),std::fclose);if(!file)return ioError();
    std::string data(static_cast<size_t>(st.st_size),'\0');
    if(std::fread(&data[0],1,data.size(),file.get())!=data.size())return ioError();
    if(std::fgetc(file.get())!=EOF)return EINVAL;
    if(std::ferror(file.get()))return ioError();
    if(std::fclose(file.release())!=0)return ioError();
    return decode(data,record)?0:EINVAL;
}
int erase(const std::string &directory,const Entry &expected,Hooks hooks) {
    if(!validFilename(expected.filename))return EINVAL;
    if(stopped(hooks))return ECANCELED;
    Record current;const int error=read(directory,expected.filename,current);
    if(error)return error;
    // The selected filename and contents must still describe the same record.
    const auto actual=encode(current),selected=encode(expected.record);
    if(actual.empty() || selected.empty())return ENOMEM;
    if(actual!=selected)return ESTALE;
    if(stopped(hooks))return ECANCELED;
    return unlink((directory+"/"+expected.filename).c_str())==0?0:ioError();
}
Page list(const std::string &directory,const std::string &before,Hooks hooks) {
    Page page;if(!before.empty() && !validFilename(before)){page.error=EINVAL;return page;}
    std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(directory.c_str()),closedir);
    if(!dir){if(errno!=ENOENT)page.error=ioError();return page;}
    std::vector<std::string> names;names.reserve(PageSize+1);
    for(;;) {
        if(stopped(hooks)){page.error=ECANCELED;return page;}
        errno=0;auto entry=readdir(dir.get());
        if(!entry){if(errno)page.error=errno;break;}
        const std::string name=entry->d_name;
        if(!validFilename(name) || (!before.empty() && name>=before))continue;
        names.insert(std::lower_bound(names.begin(),names.end(),name,std::greater<std::string>()),name);
        if(names.size()>PageSize){page.more=true;names.pop_back();}
    }
    dir.reset();
    for(const auto &name:names) {
        if(stopped(hooks)){page.error=ECANCELED;return page;}
        page.cursor=name;Record record;
        const int error=read(directory,name,record);
        if(!error)page.entries.push_back({name,record});
        else if(error==EINVAL || error==ENOENT)++page.skipped;
        else {page.error=error;break;}
    }
    return page;
}
} // namespace core::speed::store
