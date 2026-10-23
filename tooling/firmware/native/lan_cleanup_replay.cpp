#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
constexpr int ESP_OK=0,ESP_ERR_WIFI_NOT_STARTED=1,ESP_ERR_WIFI_NOT_INIT=2,ESP_ERR_TIMEOUT=3,ESP_EVENT_ANY_ID=-1,WIFI_EVENT=4,YUMO_LAN_FENCE=5;
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
enum class LanPhase{Off,Failed,Stopping};
struct State{int error=0;LanPhase phase=LanPhase::Stopping;};static State current;
static bool radio=true,driver=true,stopIssued=false,recovering=false,fencePosted=false;
// Existing dedicated-radio cleanup cases; shared STA has its own LAN replay.
static bool borrowed=false,napt=false;
static uint32_t apAddress=0,dnsAddress=0,upstreamAddress=0,upstreamGateway=0;
static int64_t nextRoute=0;
constexpr int WIFI_MODE_STA=1;
static int stopForwarding(){napt=false;return ESP_OK;}
static int esp_wifi_set_mode(int){CHECK(borrowed);return ESP_OK;}
static std::atomic<bool> stopped{false},started{false},clientsChanged{false},fenceReached{false};
static int64_t deadline=10,retryAt=0,nextSample=0,lastSample=0,now=0;
static int events=1,fenceEvents=0,ap=1,meter=0,previous=0;
static int stopError=0,deinitError=0,postError=0,unregisterError=0,deinits=0,destroyed=0,released=0;
static void stopMeter(){}
static int64_t esp_timer_get_time(){return now;}
static int esp_wifi_stop(){return stopError;}
static int esp_wifi_deinit(){++deinits;return deinitError;}
static void radioEvent(){}
static int esp_event_handler_instance_register(int,int,void(*)(),void*,int *out){*out=2;return 0;}
static int esp_event_post(int,int,void*,int,int){return postError;}
static int esp_event_handler_instance_unregister(int,int,int){return unregisterError;}
static void esp_netif_destroy_default_wifi(int){CHECK(!driver&&!events&&!fenceEvents);++destroyed;}
struct Connectivity{static void releaseFromHotspot(){++released;}};
// Production uses pointer handles. Integers here retain only presence/absence.
#include "cleanup.inc"
static void reset(){radio=driver=true;stopIssued=recovering=fencePosted=false;stopped=started=clientsChanged=fenceReached=false;deadline=10;retryAt=nextSample=lastSample=now=0;events=ap=1;fenceEvents=0;stopError=deinitError=postError=unregisterError=deinits=destroyed=released=0;current={};}
int main(){
 for(int i=0;i<100;++i){
  reset();CHECK(!cleanup()&&stopIssued&&!released);stopped=true;CHECK(cleanup()&&destroyed==1&&released==1);
  reset();CHECK(!cleanup());now=11;deinitError=99;CHECK(!cleanup()&&driver&&!released);
  now+=1000001;deinitError=0;postError=99;CHECK(!cleanup()&&!driver&&recovering&&!released);
  postError=0;CHECK(!cleanup()&&fencePosted&&!released);CHECK(!cleanup()&&!released);
  fenceReached=true;unregisterError=99;CHECK(!cleanup()&&!released);unregisterError=0;
  CHECK(cleanup()&&destroyed==1&&released==1&&!recovering&&!fencePosted);
  reset();stopError=ESP_ERR_WIFI_NOT_STARTED;CHECK(cleanup()&&released==1);
 }
 std::printf("%u hotspot cleanup checks; 100 cycles with missing STOP, driver and event failures\n",checks);
}
