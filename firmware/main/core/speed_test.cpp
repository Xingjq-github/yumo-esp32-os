#include "speed_test.hpp"
#include "connectivity.hpp"
#include "power_mgr.hpp"
#include "system_service.hpp"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <strings.h>

namespace core::speed {
namespace {
std::atomic<bool> testing{false},listing{false};
constexpr size_t BufferBytes=8192;
void publish(Job &job,const Snapshot &state){std::lock_guard<std::mutex> lock(job.mutex);job.published=state;}
bool stopped(Job &job){return job.cancelled.load() || sys::Power::sleepPending() || !sys::Power::isScreenOn();}
bool cancelled(void *context){return stopped(*static_cast<Job *>(context));}
bool historyCancelled(void *context){return static_cast<HistoryJob *>(context)->cancelled.load() || sys::Power::sleepPending();}
bool deleteCancelled(void *context){return static_cast<DeleteJob *>(context)->cancelled.load() || sys::Power::sleepPending() || !sys::Power::isScreenOn();}
void cleanText(char *out,size_t size,const char *source) {
    std::snprintf(out,size,"%s",source);
    for(size_t i=0;out[i];++i)if(static_cast<unsigned char>(out[i])<32 || out[i]==127)out[i]=' ';
}
bool sameNetwork(const Record &record) {
    const auto wifi=sys::Connectivity::snapshot();
    if(wifi.state!=sys::WifiState::Connected || wifi.ip!=record.ip)return false;
    wifi_ap_record_t ap{};if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)return false;
    char bssid[18];std::snprintf(bssid,sizeof(bssid),"%02X:%02X:%02X:%02X:%02X:%02X",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
    return !std::strcmp(bssid,record.bssid);
}
class Http {
public:
    Http(Job &job,Snapshot &state):job(job),state(state) {
        esp_http_client_config_t config{};
        config.url=DownloadUrl;config.event_handler=event;config.user_data=this;
        config.crt_bundle_attach=esp_crt_bundle_attach;
        config.skip_cert_common_name_check=false;config.disable_auto_redirect=true;
        config.timeout_ms=3000;config.buffer_size=4096;config.buffer_size_tx=2048;
        config.keep_alive_enable=true;
        client=esp_http_client_init(&config);
        buffer=static_cast<char *>(heap_caps_malloc(BufferBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(buffer)esp_fill_random(buffer,BufferBytes);
        if(client) {
            ready=esp_http_client_set_header(client,"Accept-Encoding","identity")==ESP_OK &&
                  esp_http_client_set_header(client,"Cache-Control","no-cache, no-store")==ESP_OK &&
                  esp_http_client_set_header(client,"User-Agent","Yumo-speedtest/1")==ESP_OK &&
                  esp_http_client_set_header(client,"Content-Type","application/octet-stream")==ESP_OK;
        }
    }
    ~Http(){if(client)esp_http_client_cleanup(client);heap_caps_free(buffer);}
    explicit operator bool()const{return client && buffer && ready;}
    Sample request(bool upload,uint32_t bytes,int64_t runDeadline) {
        Sample result;result.requested=bytes;sample=&result;encoded=false;ackSeen=ackInvalid=closeAfter=false;
        state.currentBytes=0;state.liveValid=false;state.currentMbps=0;
        if(state.phase==Phase::Download || state.phase==Phase::Upload)point(-1);
        publish(job,state);
        char url[180];
        if(upload)std::snprintf(url,sizeof(url),"%s?nonce=%08lx",UploadUrl,static_cast<unsigned long>(esp_random()));
        else std::snprintf(url,sizeof(url),"%s?bytes=%lu&nonce=%08lx",DownloadUrl,static_cast<unsigned long>(bytes),static_cast<unsigned long>(esp_random()));
        const int64_t begin=esp_timer_get_time();openAt=requestAt=begin;lastPublish=lastYield=rateAt=begin;rateBytes=0;
        deadline=std::min(begin+RequestLimitUs,runDeadline);
        result.reused=true;
        auto fail=[&](Error e,int native=0){result.error=e;result.nativeError=native;};
        if(stopped(job))fail(Error::Cancelled);
        else if(!sameNetwork(state.record))fail(Error::NetworkChanged);
        else if(begin>=deadline)fail(Error::Timeout);
        else if(esp_http_client_set_url(client,url)!=ESP_OK || esp_http_client_set_method(client,upload?HTTP_METHOD_POST:HTTP_METHOD_GET)!=ESP_OK)fail(Error::Memory);
        else {
            const auto opened=esp_http_client_open(client,upload?int(bytes):0);
            if(opened!=ESP_OK)fail(Error::Connect,opened);
            if(result.error==Error::None && upload) {
                while(result.bytes<bytes && continuing(result)) {
                    // Small writes stay within the socket timeout on slow uplinks.
                    // A failed SDK write can hide a partial send: never resend it.
                    const int want=std::min<uint64_t>(2048,bytes-result.bytes);
                    const int written=esp_http_client_write(client,buffer,want);
                    if(written<=0){fail(Error::Write,written);break;}
                    result.bytes+=written;state.record.transferredBytes+=written;progress(result.bytes);
                }
            }
            if(result.error==Error::None && continuing(result)) {
                const auto length=esp_http_client_fetch_headers(client);
                result.httpStatus=std::max(0,esp_http_client_get_status_code(client));
                if(length<0)fail(Error::Headers,int(length));
                else if(result.httpStatus!=200)fail(Error::Http);
                else if(encoded)fail(Error::Encoding);
                else if(upload && (!ackSeen || ackInvalid || result.acknowledged!=bytes))fail(Error::UploadAck);
                else if(!upload && !esp_http_client_is_chunked_response(client) && length!=bytes)fail(Error::DownloadLength);
                else if(upload && length>4096)fail(Error::ResponseLength);
                else {
                    uint64_t received=0;
                    while(continuing(result)) {
                        const int n=esp_http_client_read(client,buffer,BufferBytes);
                        if(n<0){fail(Error::Read,n);break;}
                        if(!n) {
                            if(!esp_http_client_is_complete_data_received(client))fail(Error::ResponseLength);
                            break;
                        }
                        received+=n;
                        if(!upload){result.bytes+=n;state.record.transferredBytes+=n;progress(result.bytes);}
                        if(received>(upload?4096u:bytes)){fail(upload?Error::ResponseLength:Error::DownloadLength);break;}
                    }
                    if(result.error==Error::None && result.bytes!=bytes)fail(Error::DownloadLength);
                }
            }
        }
        const int64_t end=esp_timer_get_time();
        result.elapsedUs=std::max<int64_t>(0,end-requestAt);
        // Never include a sample which crossed a network change or cancellation.
        if(stopped(job))fail(Error::Cancelled);
        else if(!sameNetwork(state.record))fail(Error::NetworkChanged);
        else if(end>deadline && result.error==Error::None)fail(Error::Timeout);
        if(result.error==Error::None && (!result.headersUs || !result.elapsedUs))fail(Error::Timing);
        result.complete=result.error==Error::None;
        state.currentBytes=result.bytes;
        if(result.bytes>rateBytes)observe(result.bytes,end);
        ESP_LOGI("speed_test","request phase=%u upload=%u want=%lu bytes=%llu ack=%lu http=%d native=%d error=%u headers_us=%lld elapsed_us=%lld connect_us=%lld reused=%u",
                 unsigned(state.phase),unsigned(upload),static_cast<unsigned long>(bytes),static_cast<unsigned long long>(result.bytes),
                 static_cast<unsigned long>(result.acknowledged),result.httpStatus,result.nativeError,unsigned(result.error),
                 static_cast<long long>(result.headersUs),static_cast<long long>(result.elapsedUs),static_cast<long long>(result.connectUs),unsigned(result.reused));
        if(!result.complete || closeAfter || !esp_http_client_is_persistent_connection(client))esp_http_client_close(client);
        sample=nullptr;return result;
    }
private:
    static esp_err_t event(esp_http_client_event_t *e) {
        auto &self=*static_cast<Http *>(e->user_data);auto s=self.sample;if(!s)return ESP_OK;
        const auto now=esp_timer_get_time();
        if(e->event_id==HTTP_EVENT_ON_CONNECTED) {s->reused=false;s->connectUs=now-self.openAt;self.requestAt=self.rateAt=now;}
        else if(e->event_id==HTTP_EVENT_ON_HEADERS_COMPLETE)s->headersUs=now-self.requestAt;
        else if(e->event_id==HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
            if(!strcasecmp(e->header_key,"Content-Encoding") && strcasecmp(e->header_value,"identity"))self.encoded=true;
            if(!strcasecmp(e->header_key,"Connection") && !strcasecmp(e->header_value,"close"))self.closeAfter=true;
            if(!strcasecmp(e->header_key,"cf-meta-upload-bytes")) {
                if(self.ackSeen)self.ackInvalid=true;
                self.ackSeen=true;uint64_t count=0;
                if(!*e->header_value)self.ackInvalid=true;
                for(const char *p=e->header_value;*p;++p) {
                    if(*p<'0'||*p>'9'){self.ackInvalid=true;break;}
                    count=count*10+(*p-'0');if(count>MaxBytes){self.ackInvalid=true;break;}
                }
                if(!self.ackInvalid)s->acknowledged=uint32_t(count);
            }
            if(!strcasecmp(e->header_key,"cf-ray"))cleanText(self.state.record.serverTrace,sizeof(self.state.record.serverTrace),e->header_value);
        }
        return ESP_OK;
    }
    bool continuing(Sample &s) {
        if(stopped(job)){s.error=Error::Cancelled;return false;}
        if(esp_timer_get_time()>=deadline){s.error=Error::Timeout;return false;}
        return true;
    }
    void progress(uint64_t bytes) {
        const auto now=esp_timer_get_time();
        if(now-lastPublish>=250000){observe(bytes,now);state.currentBytes=bytes;publish(job,state);lastPublish=now;}
        // Bounded yield for idle tasks/watchdogs; no per-packet display work.
        if(now-lastYield>=50000){vTaskDelay(1);lastYield=esp_timer_get_time();}
    }
    void point(int32_t value) {
        if(state.liveCount==state.liveKbps.size()) {
            for(size_t i=1;i<state.liveKbps.size();++i)state.liveKbps[i-1]=state.liveKbps[i];
            --state.liveCount;
        }
        state.liveKbps[state.liveCount++]=value;
    }
    void observe(uint64_t bytes,int64_t now) {
        if((state.phase==Phase::Download || state.phase==Phase::Upload) && now>rateAt) {
            state.currentMbps=mbps(bytes-rateBytes,now-rateAt);state.liveValid=true;
            point(static_cast<int32_t>(std::min(1000000.0,state.currentMbps*1000.0)));
        }
        rateAt=now;rateBytes=bytes;
    }
    Job &job;Snapshot &state;
    esp_http_client_handle_t client=nullptr;char *buffer=nullptr;Sample *sample=nullptr;
    bool ready=false,encoded=false,ackSeen=false,ackInvalid=false,closeAfter=false;
    int64_t openAt=0,requestAt=0,deadline=0,lastPublish=0,lastYield=0,rateAt=0;
    uint64_t rateBytes=0;
};
bool terminal(Error error){return error==Error::Cancelled || error==Error::NetworkChanged;}
// Independent ICMP worker. Socket polling is bounded; monotonic timestamps are
// unaffected by NTP corrections. A stage transition discards outstanding probes.
class Probes {
    struct State {
        std::mutex mutex;
        std::atomic<bool> cancel{false},done{false};
        std::array<ProbeRun,6> runs{};
        int stage=-1,rssiMin=-127,rssiMax=-127;
        unsigned rssiCount=0;
        uint32_t source=0;
        int startupError=0;
    };
    struct Target {
        int socket=-1;
        sockaddr_in address{};
        uint16_t id=0,sequence=0;
        int64_t sentAt=0,nextAt=0;
        bool pending=false;
        uint8_t payload[32]{};
    };
    std::shared_ptr<State> data;
    static uint16_t checksum(const uint8_t *bytes,size_t size) {
        uint32_t sum=0;for(size_t i=0;i+1<size;i+=2)sum+=(uint16_t(bytes[i])<<8)|bytes[i+1];
        if(size&1)sum+=uint16_t(bytes[size-1])<<8;
        while(sum>>16)sum=(sum&65535)+(sum>>16);
        return uint16_t(~sum);
    }
    static void record(State &s,int stage,unsigned target,int32_t us,int rssi) {
        std::lock_guard<std::mutex> lock(s.mutex);
        if(s.stage!=stage || stage<0)return;
        auto &run=s.runs[stage*2+target];if(run.count>=ProbeCapacity)return;
        run.samples[run.count++]={us,int16_t(rssi)};
    }
    static int signal(State &s) {
        wifi_ap_record_t ap{};if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)return -127;
        std::lock_guard<std::mutex> lock(s.mutex);
        if(!s.rssiCount)s.rssiMin=s.rssiMax=ap.rssi;
        else {s.rssiMin=std::min(s.rssiMin,int(ap.rssi));s.rssiMax=std::max(s.rssiMax,int(ap.rssi));}
        ++s.rssiCount;return ap.rssi;
    }
    static void worker(void *argument) {
        {
            std::unique_ptr<std::shared_ptr<State>> owner(static_cast<std::shared_ptr<State> *>(argument));
            auto s=std::move(*owner);owner.reset();
            Target targets[2];
            for(unsigned i=0;i<2;++i) {
                auto &t=targets[i];t.address.sin_family=AF_INET;
                t.address.sin_addr.s_addr=inet_addr(s->runs[i].target);
                t.id=uint16_t(esp_random());
                int error=0;
                if(t.address.sin_addr.s_addr==INADDR_NONE || !t.address.sin_addr.s_addr)error=EINVAL;
                else {
                    t.socket=socket(AF_INET,SOCK_RAW,IPPROTO_ICMP);
                    if(t.socket<0)error=errno?errno:EIO;
                    else {
                        timeval sendTimeout{};sendTimeout.tv_usec=100000;
                        if(setsockopt(t.socket,SOL_SOCKET,SO_SNDTIMEO,&sendTimeout,sizeof(sendTimeout))<0)error=errno?errno:EIO;
                        sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=s->source;
                        if(!error && bind(t.socket,reinterpret_cast<sockaddr *>(&local),sizeof(local))<0)error=errno?errno:EIO;
                    }
                }
                if(error) {
                    if(t.socket>=0){close(t.socket);t.socket=-1;}
                    std::lock_guard<std::mutex> lock(s->mutex);
                    for(unsigned n=i;n<6;n+=2)s->runs[n].error=error;
                }
            }
            int active=-1;int64_t lastSignal=0;int rssi=-127;
            while(!s->cancel.load(std::memory_order_acquire)) {
                int stage;{std::lock_guard<std::mutex> lock(s->mutex);stage=s->stage;}
                const auto now=esp_timer_get_time();
                if(stage!=active){active=stage;for(auto &t:targets){t.pending=false;t.nextAt=now;}}
                if(now-lastSignal>=500000){rssi=signal(*s);lastSignal=now;}
                fd_set reads;FD_ZERO(&reads);int maximum=-1;
                for(unsigned i=0;i<2;++i) {
                    auto &t=targets[i];if(t.socket<0)continue;
                    FD_SET(t.socket,&reads);maximum=std::max(maximum,t.socket);
                    if(t.pending && now-t.sentAt>=800000) {
                        record(*s,active,i,-1,rssi);t.pending=false;
                    }
                    unsigned count=0;bool failed=false;
                    if(active>=0){std::lock_guard<std::mutex> lock(s->mutex);const auto &run=s->runs[active*2+i];count=run.count;failed=run.error;}
                    const unsigned limit=active==0?IdleProbeCount:ProbeCapacity;
                    if(active<0 || failed || count>=limit || t.pending || now<t.nextAt)continue;
                    uint8_t packet[40]{};packet[0]=8;++t.sequence;
                    packet[4]=t.id>>8;packet[5]=t.id;packet[6]=t.sequence>>8;packet[7]=t.sequence;
                    // Random payload prevents cached or unrelated echo matches.
                    esp_fill_random(packet+8,sizeof(packet)-8);std::memcpy(t.payload,packet+8,sizeof(t.payload));
                    const auto check=checksum(packet,sizeof(packet));packet[2]=check>>8;packet[3]=check;
                    t.sentAt=esp_timer_get_time();t.nextAt=t.sentAt+500000;
                    if(sendto(t.socket,packet,sizeof(packet),0,reinterpret_cast<sockaddr *>(&t.address),sizeof(t.address))!=int(sizeof(packet))) {
                        std::lock_guard<std::mutex> lock(s->mutex);
                        s->runs[active*2+i].error=errno?errno:EIO;
                    }else t.pending=true;
                }
                if(maximum<0){vTaskDelay(pdMS_TO_TICKS(20));continue;}
                timeval timeout{};timeout.tv_usec=20000;
                const int ready=select(maximum+1,&reads,nullptr,nullptr,&timeout);
                if(ready<0) {
                    std::lock_guard<std::mutex> lock(s->mutex);
                    for(unsigned i=0;i<2;++i)if(active>=0 && targets[i].socket>=0)s->runs[active*2+i].error=errno?errno:EIO;
                    break;
                }
                for(unsigned i=0;i<2;++i) {
                    auto &t=targets[i];if(t.socket<0 || !FD_ISSET(t.socket,&reads))continue;
                    uint8_t reply[128];sockaddr_in from{};socklen_t size=sizeof(from);
                    const int n=recvfrom(t.socket,reply,sizeof(reply),MSG_DONTWAIT,reinterpret_cast<sockaddr *>(&from),&size);
                    if(n<60 || !t.pending || from.sin_addr.s_addr!=t.address.sin_addr.s_addr)continue;
                    const unsigned header=(reply[0]&15)*4;
                    if((reply[0]>>4)!=4 || header<20 || header+40>unsigned(n))continue;
                    const auto echo=reply+header;
                    if(echo[0]!=0 || echo[1]!=0 || std::memcmp(echo+8,t.payload,sizeof(t.payload)) || checksum(echo,n-header)!=0 ||
                       ((uint16_t(echo[4])<<8)|echo[5])!=t.id || ((uint16_t(echo[6])<<8)|echo[7])!=t.sequence)continue;
                    const auto elapsed=esp_timer_get_time()-t.sentAt;
                    record(*s,active,i,int32_t(std::max<int64_t>(0,elapsed)),rssi);t.pending=false;
                }
            }
            for(auto &t:targets)if(t.socket>=0)close(t.socket);
            s->done.store(true,std::memory_order_release);
        }
        vTaskDeleteWithCaps(nullptr);
    }
public:
    Probes(const Record &r):data(std::make_shared<State>()) {
        esp_netif_ip_info_t ip{};auto netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if(!netif || esp_netif_get_ip_info(netif,&ip)!=ESP_OK || ip.ip.addr!=inet_addr(r.ip)) {
            data->startupError=ENETDOWN;data->done=true;
        }else data->source=ip.ip.addr;
        char gateway[16]{};if(ip.gw.addr)esp_ip4addr_ntoa(&ip.gw,gateway,sizeof(gateway));
        for(unsigned n=0;n<6;++n)std::snprintf(data->runs[n].target,16,"%s",n%2?InternetProbeTarget:gateway);
        if(data->startupError){for(auto &run:data->runs)run.error=data->startupError;return;}
        auto owner=new std::shared_ptr<State>(data);
        if(xTaskCreateWithCaps(worker,"speed_ping",4096,owner,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
            delete owner;for(auto &run:data->runs)run.error=ENOMEM;data->done=true;
        }
    }
    ~Probes(){stop();}
    void stage(int value){std::lock_guard<std::mutex> lock(data->mutex);data->stage=value;}
    void snapshot(Record &r) {
        std::lock_guard<std::mutex> lock(data->mutex);r.probes=data->runs;
        r.rssiMin=data->rssiMin;r.rssiMax=data->rssiMax;r.rssiCount=data->rssiCount;
    }
    bool idleComplete() {
        std::lock_guard<std::mutex> lock(data->mutex);
        for(unsigned i=0;i<2;++i)if(!data->runs[i].error && data->runs[i].count<IdleProbeCount)return false;
        return true;
    }
    void stop() {
        data->cancel.store(true,std::memory_order_release);
        while(!data->done.load(std::memory_order_acquire))vTaskDelay(pdMS_TO_TICKS(20));
    }
};
void measure(Job &job,Snapshot &state) {
    auto &r=state.record;
    const auto wifi=sys::Connectivity::snapshot();
    r.epoch=std::time(nullptr);std::tm local{};time_t epoch=r.epoch;localtime_r(&epoch,&local);
    std::strftime(r.time,sizeof(r.time),"%Y-%m-%d %H:%M:%S %z",&local);
    cleanText(r.ssid,sizeof(r.ssid),wifi.ssid.c_str());cleanText(r.ip,sizeof(r.ip),wifi.ip.c_str());
    r.rssiStart=r.rssiEnd=wifi.rssi;
    r.clockSynced=sys::SystemService::timeSync().state==sys::TimeSyncState::Synced;
    if(wifi.state!=sys::WifiState::Connected){r.error=Error::Wifi;return;}
    wifi_ap_record_t ap{};
    if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK){r.error=Error::Wifi;return;}
    r.channel=ap.primary;
    std::snprintf(r.bssid,sizeof(r.bssid),"%02X:%02X:%02X:%02X:%02X:%02X",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
    if(r.epoch<1704067200 || r.epoch>4102444800LL){r.error=Error::Clock;return;}
    const auto begin=esp_timer_get_time(),deadline=begin+RunLimitUs;
    Http http(job,state);
    if(!http){r.error=Error::Memory;return;}
    publish(job,state);
    r.qualityVersion=1;
    // The first warm-up sizes download requests. Direction warm-ups and their
    // traffic are excluded from accepted throughput and loaded latency.
    const auto warm=http.request(false,FirstBytes,deadline);
    r.warmup[0]=warm;r.warmupCount=1;
    Probes probes(r);probes.stage(0);
    state.phase=Phase::Latency;
    const auto idleBegin=esp_timer_get_time();
    for(unsigned i=0;i<LatencyCount;++i) {
        state.sample=i+1;probes.snapshot(r);publish(job,state);
        r.latency[i]=http.request(false,0,deadline);r.latencyCount=i+1;
        if(!r.latency[i].complete)r.error=r.latency[i].error;
        if(terminal(r.error) || esp_timer_get_time()>=deadline)break;
        for(unsigned n=0;n<10 && !stopped(job);++n)vTaskDelay(pdMS_TO_TICKS(20));
    }
    while(!terminal(r.error) && !stopped(job) && sameNetwork(r) && !probes.idleComplete() &&
          esp_timer_get_time()-idleBegin<20000000 && esp_timer_get_time()<deadline) {
        probes.snapshot(r);publish(job,state);vTaskDelay(pdMS_TO_TICKS(100));
    }
    probes.stage(-1);
    for(bool upload:{false,true}) {
        if(terminal(r.error) || stopped(job) || !sameNetwork(r) || esp_timer_get_time()>=deadline)break;
        const unsigned direction=upload?1:0;
        state.phase=upload?Phase::Upload:Phase::Download;
        state.liveCount=0;state.liveValid=false;state.directionElapsedUs=0;
        Sample directionWarm=warm;
        if(upload && r.transferredBytes+FirstBytes<=TrafficLimitBytes) {
            directionWarm=http.request(true,FirstBytes,deadline);
            r.warmup[1]=directionWarm;r.warmupCount=2;
        }
        auto &samples=upload?r.upload:r.download;auto &count=upload?r.uploadCount:r.downloadCount;
        uint32_t bytes=nextBytes(directionWarm);unsigned failures=0;
        const auto directionBegin=esp_timer_get_time();
        const auto directionDeadline=std::min(deadline,directionBegin+DirectionLimitUs);
        probes.stage(upload?2:1);
        for(unsigned i=0;i<TransferCount;++i) {
            const auto allowance=upload?TrafficLimitBytes:TrafficLimitBytes/2;
            const auto remaining=r.transferredBytes<allowance?allowance-r.transferredBytes:0;
            if(remaining<MinBytes){r.trafficLimited[direction]=true;break;}
            bytes=uint32_t(std::min<uint64_t>(bytes,remaining));
            state.sample=i+1;state.directionElapsedUs=esp_timer_get_time()-directionBegin;
            probes.snapshot(r);publish(job,state);
            samples[i]=http.request(upload,bytes,directionDeadline);count=i+1;
            if(!samples[i].complete){r.error=samples[i].error;++failures;}else failures=0;
            bytes=nextBytes(samples[i]);
            if(transferSatisfied(samples,count) || terminal(r.error) || failures>=3 || stopped(job) ||
               esp_timer_get_time()>=directionDeadline)break;
        }
        probes.stage(-1);r.directionUs[direction]=esp_timer_get_time()-directionBegin;
    }
    probes.stop();probes.snapshot(r);
    if(stopped(job))r.error=Error::Cancelled;
    else if(!sameNetwork(r))r.error=Error::NetworkChanged;
    else if(esp_timer_get_time()>=deadline)r.error=Error::Timeout;
    r.rssiEnd=sys::Connectivity::snapshot().rssi;r.durationUs=esp_timer_get_time()-begin;
}
void persist(Job &job,Snapshot &state) {
    // Cancellation closes network/file resources promptly; a partial result
    // stays visible in RAM but is never presented as a saved completed test.
    if(stopped(job) || state.record.epoch<1704067200 || state.record.epoch>4102444800LL)return;
    state.phase=Phase::Saving;publish(job,state);
    sd::Session media(job.sdAccess);
    if(!media){state.storageError=media.error();return;}
    if(!media.claimMutation()){state.storageError=EBUSY;return;}
    if(!sd::ensure(sd::SpeedTests)){state.storageError=errno?errno:EIO;media.reportError(state.storageError);return;}
    std::tm local{};time_t epoch=state.record.epoch;localtime_r(&epoch,&local);
    char prefix[20],filename[48];std::strftime(prefix,sizeof(prefix),"%Y%m%d-%H%M%S",&local);
    std::snprintf(filename,sizeof(filename),"%s-%08lx%08lx.json",prefix,static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));
    state.storageError=store::save(sd::SpeedTests,filename,state.record,{&job,cancelled});
    state.saved=!state.storageError;media.reportError(state.storageError);
    if(state.saved)std::snprintf(state.filename,sizeof(state.filename),"%s",filename);
}
void testWorker(void *argument) {
    {
        std::unique_ptr<std::shared_ptr<Job>> owner(static_cast<std::shared_ptr<Job> *>(argument));auto job=std::move(*owner);owner.reset();
        Snapshot state;
        {
            sys::Power::WorkGuard work;
            try {measure(*job,state);persist(*job,state);}catch(const std::bad_alloc &){state.record.error=Error::Memory;}
        }
        state.phase=Phase::Finished;publish(*job,state);job->done.store(true,std::memory_order_release);
        ESP_LOGI("speed_test","finished error=%u latency=%u download=%u upload=%u saved=%u storage=%d",unsigned(state.record.error),
                 latency(state.record).valid,throughput(state.record.download,state.record.downloadCount).valid,
                 throughput(state.record.upload,state.record.uploadCount).valid,unsigned(state.saved),state.storageError);
    }
    testing.store(false,std::memory_order_release);vTaskDeleteWithCaps(nullptr);
}
void historyWorker(void *argument) {
    {
        std::unique_ptr<std::shared_ptr<HistoryJob>> owner(static_cast<std::shared_ptr<HistoryJob> *>(argument));auto job=std::move(*owner);owner.reset();
        {
            sys::Power::WorkGuard work;
            try {
                sd::Session media(job->sdAccess);
                if(!media)job->page.error=media.error();
                else {job->page=store::list(sd::SpeedTests,job->before,{job.get(),historyCancelled});if(!job->page.error)job->cardGeneration=media.generation();media.reportError(job->page.error);}
            }catch(const std::bad_alloc &){job->page.error=ENOMEM;}
        }
        job->done.store(true,std::memory_order_release);
    }
    listing.store(false,std::memory_order_release);vTaskDeleteWithCaps(nullptr);
}
void deleteWorker(void *argument) {
    {
        std::unique_ptr<std::shared_ptr<DeleteJob>> owner(static_cast<std::shared_ptr<DeleteJob> *>(argument));auto job=std::move(*owner);owner.reset();
        {
            sys::Power::WorkGuard work;
            try {
                if(deleteCancelled(job.get()))job->error=ECANCELED;
                else {
                    sd::Session media(job->sdAccess);
                    if(!media)job->error=media.error();
                    else if(media.generation()!=job->cardGeneration)job->error=ESTALE;
                    else if(!media.claimMutation())job->error=EBUSY;
                    else {job->error=store::erase(sd::SpeedTests,job->entry,{job.get(),deleteCancelled});media.reportError(job->error);}
                }
            }catch(const std::bad_alloc &){job->error=ENOMEM;}
        }
        // Publish completion only after releasing admission for the list refresh.
        listing.store(false,std::memory_order_release);
        job->done.store(true,std::memory_order_release);
    }
    vTaskDeleteWithCaps(nullptr);
}
}
bool idle(){return !testing.load(std::memory_order_acquire) && !listing.load(std::memory_order_acquire);}
std::shared_ptr<Job> start() {
    if(sys::Power::sleepPending() || !sys::Power::isScreenOn() || sys::Connectivity::wifiState()!=sys::WifiState::Connected)return {};
    bool expected=false;if(!testing.compare_exchange_strong(expected,true))return {};
    try {
        auto job=std::make_shared<Job>();job->sdAccess=sd::requestAccess();
        auto owner=new std::shared_ptr<Job>(job);
        if(xTaskCreateWithCaps(testWorker,"speed_test",24576,owner,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete owner;
    }catch(const std::bad_alloc &){}
    testing.store(false,std::memory_order_release);return {};
}
std::shared_ptr<HistoryJob> history(const std::string &before) {
    if(sys::Power::sleepPending())return {};
    bool expected=false;if(!listing.compare_exchange_strong(expected,true))return {};
    try {
        auto job=std::make_shared<HistoryJob>();job->before=before;job->sdAccess=sd::requestAccess(true);
        auto owner=new std::shared_ptr<HistoryJob>(job);
        if(xTaskCreateWithCaps(historyWorker,"speed_history",24576,owner,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete owner;
    }catch(const std::bad_alloc &){}
    listing.store(false,std::memory_order_release);return {};
}
std::shared_ptr<DeleteJob> removeHistory(const store::Entry &entry,uint64_t cardGeneration) {
    if(!cardGeneration || !store::validFilename(entry.filename) || sys::Power::sleepPending() || !sys::Power::isScreenOn() || testing.load())return {};
    bool expected=false;if(!listing.compare_exchange_strong(expected,true))return {};
    try {
        auto job=std::make_shared<DeleteJob>();job->entry=entry;job->cardGeneration=cardGeneration;job->sdAccess=sd::requestAccess();
        auto owner=new std::shared_ptr<DeleteJob>(job);
        if(xTaskCreateWithCaps(deleteWorker,"speed_delete",24576,owner,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete owner;
    }catch(const std::bad_alloc &){}
    listing.store(false,std::memory_order_release);return {};
}
} // namespace core::speed
