#include <atomic>
#include <mutex>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
struct FakeTimeval {int64_t tv_sec;long tv_usec;};
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_TIMEOUT=2,ESP_ERR_NOT_FINISHED=3;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static int64_t mono=0,wall=1770000000,saved=0;
static int initError=0,startError=0,waitError=ESP_ERR_TIMEOUT,setError=0,events=0,stops=0,saves=0;
static bool client=false,pendingSample=false,flushOk=true;
static std::vector<std::string> servers;
static std::mutex snapshotMutex;
enum class WifiState {Off,Connected};
struct Connectivity {inline static WifiState state=WifiState::Off;static WifiState wifiState(){return state;}};
struct Power {inline static bool asleep=false,screen=true;static bool sleepPending(){return asleep;}static bool isScreenOn(){return screen;}};
struct Settings {static void saveClock(int64_t t){saved=t;++saves;}static bool flush(bool){return flushOk;}};
struct ClockDiscipline {
    inline static unsigned fenced=0,manual=0;
    static void networkStopped(){CHECK(!client);++fenced;}
    static void manualTimeChanged(){CHECK(!client);++manual;}
};
enum class EventType {TimeSynced};
struct Event{EventType type;};
struct EventBus {static void publish(Event){++events;}};
int64_t esp_timer_get_time(){return mono;}
time_t fakeWallTime(void*){return wall;}
int fakeSetTime(FakeTimeval *v,void*) {if(setError)return -1;wall=v->tv_sec;return 0;}
struct esp_sntp_config_t {const char *server;bool start=true;};
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(s) esp_sntp_config_t{s,true}
int esp_netif_sntp_init(esp_sntp_config_t *c){CHECK(!client);CHECK(!c->start);servers.emplace_back(c->server);if(initError)return initError;client=true;return ESP_OK;}
int esp_netif_sntp_start(){CHECK(client);return startError;}
void esp_netif_sntp_deinit(){CHECK(client);++stops;client=false;pendingSample=false;}
int esp_netif_sntp_sync_wait(int ticks){CHECK(ticks==0);CHECK(client);if(pendingSample){pendingSample=false;return ESP_OK;}return waitError;}
#include "network_time.inc"
static void reset() {
    stopTimeClient();timeSyncActive=false;timeSyncQueued=false;timeState={};timeDeadline=nextTimeSync=nextClockSave=0;timeServer=0;timeWasOnline=false;
    mono=0;wall=1770000000;saved=0;initError=startError=setError=events=stops=saves=0;waitError=ESP_ERR_TIMEOUT;
    pendingSample=false;flushOk=true;servers.clear();Connectivity::state=WifiState::Off;Power::asleep=false;Power::screen=true;
}
int main() {
    reset();CHECK(syncTime()==ESP_ERR_INVALID_STATE);CHECK(timeState.state==TimeSyncState::Offline);CHECK(!timeSyncActive);CHECK(!client);
    Connectivity::state=WifiState::Connected;pollTimeSync();CHECK(timeSyncActive);CHECK(client);CHECK(servers.size()==1);
    const auto revision=timeState.revision;const auto deadline=timeDeadline;
    for(int i=0;i<300;++i) {mono+=1000;CHECK(syncTime()==ESP_OK);CHECK(timeDeadline==deadline);CHECK(timeState.revision==revision);}
    wall+=86400;pendingSample=true;pollTimeSync();CHECK(timeState.state==TimeSyncState::Synced);CHECK(saved==wall);CHECK(events==1);CHECK(!client);CHECK(!timeSyncActive);
    mono+=60000000;pollTimeSync();CHECK(servers.size()==1);
    Connectivity::state=WifiState::Off;pollTimeSync();Connectivity::state=WifiState::Connected;pollTimeSync();CHECK(servers.size()==2);CHECK(timeSyncActive);
    Power::asleep=true;pollTimeSync();CHECK(timeState.state==TimeSyncState::Cancelled);CHECK(!client);CHECK(!timeSyncActive);
    // Timeout and fallback are bounded under different service scheduling delays.
    for(int jitter=0;jitter<250;++jitter) {
        reset();Connectivity::state=WifiState::Connected;pollTimeSync();
        for(unsigned s=0;s<3;++s) {
            CHECK(timeServer==s);mono=timeDeadline-1;pollTimeSync();CHECK(timeServer==s);CHECK(timeSyncActive);
            mono=timeDeadline+jitter*1000;pollTimeSync();
        }
        CHECK(timeState.state==TimeSyncState::Failed);CHECK(timeState.error==ESP_ERR_TIMEOUT);CHECK(!timeSyncActive);CHECK(!client);
        CHECK(servers.size()==3);CHECK(stops==3);CHECK(saved==0);CHECK(events==0);
        mono=nextTimeSync-1;pollTimeSync();CHECK(servers.size()==3);
        mono=nextTimeSync;Power::screen=false;pollTimeSync();CHECK(servers.size()==3);
        Power::screen=true;pollTimeSync();CHECK(servers.size()==4);CHECK(timeSyncActive);
    }
    reset();Connectivity::state=WifiState::Connected;pollTimeSync();Connectivity::state=WifiState::Off;pollTimeSync();
    CHECK(timeState.state==TimeSyncState::Offline);CHECK(!client);CHECK(!timeSyncActive);CHECK(!timeWasOnline);
    // A stale completion is discarded before manual set, and cannot win later.
    reset();Connectivity::state=WifiState::Connected;pollTimeSync();pendingSample=true;
    CHECK(applyClock(1780000000)==ESP_OK);CHECK(wall==1780000000);CHECK(saved==wall);CHECK(!pendingSample);CHECK(!client);CHECK(!timeSyncActive);CHECK(timeState.state==TimeSyncState::Manual);
    mono+=1000000;pollTimeSync();CHECK(servers.size()==1);CHECK(wall==1780000000);CHECK(events==1);
    CHECK(syncTime()==ESP_OK);CHECK(client);pendingSample=true;wall+=120;pollTimeSync();CHECK(timeState.state==TimeSyncState::Synced);CHECK(events==2);
    reset();Connectivity::state=WifiState::Connected;initError=ESP_FAIL;pollTimeSync();CHECK(!timeSyncActive);CHECK(!client);CHECK(timeState.state==TimeSyncState::Failed);
    reset();Connectivity::state=WifiState::Connected;startError=ESP_FAIL;pollTimeSync();CHECK(!timeSyncActive);CHECK(!client);CHECK(stops==1);
    reset();Connectivity::state=WifiState::Connected;pollTimeSync();waitError=ESP_ERR_INVALID_STATE;pollTimeSync();CHECK(!timeSyncActive);CHECK(!client);CHECK(timeState.error==ESP_ERR_INVALID_STATE);
    reset();Connectivity::state=WifiState::Connected;pollTimeSync();Power::asleep=true;pendingSample=true;pollTimeSync();CHECK(events==0);CHECK(!client);CHECK(!timeSyncActive);
    reset();Power::screen=false;Connectivity::state=WifiState::Connected;pollTimeSync();CHECK(servers.empty());Power::screen=true;pollTimeSync();CHECK(servers.size()==1);
    reset();setError=1;CHECK(applyClock(1780000000)==ESP_FAIL);CHECK(saved==0);CHECK(events==0);
    reset();flushOk=false;CHECK(applyClock(1780000000)==ESP_FAIL);CHECK(wall==1780000000);CHECK(saved==wall);
    reset();CHECK(applyClock(4090000000LL)==ESP_OK);CHECK(wall==4090000000LL);CHECK(saved==wall);
    // Minute checkpoints use monotonic time and merge a nearby explicit save.
    reset();pollClockCheckpoint(mono);CHECK(saves==1);CHECK(saved==wall);
    mono=59999999;pollClockCheckpoint(mono);CHECK(saves==1);
    CHECK(applyClock(wall+86400)==ESP_OK);CHECK(saves==2);
    mono=60000000;pollClockCheckpoint(mono);CHECK(saves==2);
    mono=119999998;pollClockCheckpoint(mono);CHECK(saves==2);
    mono=119999999;pollClockCheckpoint(mono);CHECK(saves==3);
    // NTP success restarts the minute; a failed sync does not suppress it.
    reset();pollClockCheckpoint(mono);mono=59000000;
    Connectivity::state=WifiState::Connected;CHECK(syncTime()==ESP_OK);
    pendingSample=true;pollTimeSync();CHECK(saves==2);
    mono=60000000;pollClockCheckpoint(mono);CHECK(saves==2);
    mono=118999999;pollClockCheckpoint(mono);CHECK(saves==2);
    mono=119000000;wall-=3600;pollClockCheckpoint(mono);CHECK(saves==3&&saved==wall);
    reset();pollClockCheckpoint(mono);mono=59000000;setError=1;
    CHECK(applyClock(wall+1)==ESP_FAIL);mono=60000000;pollClockCheckpoint(mono);CHECK(saves==2);
    // Failed persistence is still owned by Settings' dirty/retry mechanism;
    // explicit manual save reports the failure, without a second periodic save.
    reset();flushOk=false;CHECK(applyClock(wall+1)==ESP_FAIL);pollClockCheckpoint(mono);CHECK(saves==1);
    std::printf("%d production SNTP/checkpoint orchestration checks passed (fake transport/clock).\n",checks);
}
