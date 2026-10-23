// Replay the production measurement scheduler with deterministic HTTP results.
#include "core/speed_test.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <iostream>
using namespace core::speed;
std::tm *localtime_r(const time_t *t,std::tm *out){*out=*std::localtime(t);return out;}
int64_t fakeNow=1000;int mode=0;unsigned transferCalls=0,openProbes=0;bool network=true;
int64_t esp_timer_get_time(){return fakeNow;}
#define pdMS_TO_TICKS(n) (n)
void vTaskDelay(int ms){fakeNow+=ms*1000;}
namespace sys {
enum class WifiState {Connected,Disconnected};enum class TimeSyncState {Synced};
struct WifiSnapshot {WifiState state=WifiState::Connected;std::string ssid="test",ip="192.168.1.3";int rssi=-50;};
struct TimeSync {TimeSyncState state=TimeSyncState::Synced;};
struct Connectivity {static WifiSnapshot snapshot(){return {};}};
struct SystemService {static TimeSync timeSync(){return {};}};
}
struct wifi_ap_record_t {int rssi=-50,primary=6;unsigned char bssid[6]={1,2,3,4,5,6};};
constexpr int ESP_OK=0;
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *){return ESP_OK;}
void cleanText(char *out,size_t n,const char *text){std::snprintf(out,n,"%s",text);}
void publish(Job &,const Snapshot &){}
bool stopped(Job &job){return job.cancelled;}
bool sameNetwork(const Record &){return network;}
bool terminal(Error e){return e==Error::Cancelled || e==Error::NetworkChanged;}
struct Http {
 Job &job;Snapshot &state;
 Http(Job &j,Snapshot &s):job(j),state(s){}
 explicit operator bool()const{return true;}
 Sample request(bool upload,uint32_t bytes,int64_t deadline){
  Sample s;s.requested=bytes;s.bytes=s.acknowledged=bytes;s.httpStatus=200;s.headersUs=10000;s.elapsedUs=bytes?2000000:10000;
  if(mode==1 && bytes)s.elapsedUs=10000; // very fast: reach traffic cap
  if(mode==2 && bytes)s.elapsedUs=9000000; // slow: direction deadline, partial final request
  if(fakeNow+s.elapsedUs>deadline){s.elapsedUs=std::max<int64_t>(1,deadline-fakeNow);s.error=Error::Timeout;s.bytes=bytes/2;}
  if(bytes && (state.phase==Phase::Download || state.phase==Phase::Upload)) {
   ++transferCalls;
   if(mode==3){s.error=Error::ResponseLength;s.bytes=bytes/2;}
   if(mode==4 && transferCalls==2){job.cancelled=true;s.error=Error::Cancelled;}
   if(mode==5 && transferCalls==2){network=false;s.error=Error::NetworkChanged;}
  }
  fakeNow+=s.elapsedUs;s.complete=s.error==Error::None;
  state.record.transferredBytes+=s.bytes;return s;
 }
};
struct Probes {
 int active=-1;
 explicit Probes(const Record &){++openProbes;}
 ~Probes(){--openProbes;}
 void stage(int value){active=value;}
 bool idleComplete(){return true;}
 void snapshot(Record &r){if(active>=0)r.probes[active*2].count=20;}
 void stop(){}
};
#include "production_measure.inc"
void reset(int m){mode=m;fakeNow=1000;network=true;transferCalls=0;assert(!openProbes);}
int main(){
 for(int m=0;m<=5;++m){reset(m);Job job;Snapshot state;measure(job,state);const auto &r=state.record;
  assert(!openProbes && r.qualityVersion && r.transferredBytes<=TrafficLimitBytes && r.durationUs<=RunLimitUs);
  assert(r.downloadCount<=TransferCount && r.uploadCount<=TransferCount);
  assert(r.warmupCount==2 || m==4 || m==5);
  const auto down=throughput(r.download,r.downloadCount),up=throughput(r.upload,r.uploadCount);
  if(m==0){assert(complete(r) && down.elapsedUs==12000000 && up.elapsedUs==12000000 && down.valid==6 && up.valid==6);}
  if(m==1){assert(r.trafficLimited[0] && r.trafficLimited[1] && confidence(down)==Confidence::Limited && !complete(r));}
  if(m==2){assert(r.directionUs[0]<=DirectionLimitUs && r.directionUs[1]<=DirectionLimitUs && r.error==Error::Timeout && down.valid>0);}
  if(m==3){assert(r.downloadCount==3 && r.uploadCount==3 && !down.valid && !up.valid && r.error==Error::ResponseLength);}
  if(m==4){assert(r.error==Error::Cancelled && r.uploadCount==0);}
  if(m==5){assert(r.error==Error::NetworkChanged && r.uploadCount==0);}
 }
 std::cout<<"Production scheduler: time target, fast traffic cap, slow deadline, consecutive failures, cancellation/network switch and independent direction warm-up passed\n";
}