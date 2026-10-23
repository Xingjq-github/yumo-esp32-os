// Execute the production LAN state machine with deterministic radio/netif faults.
#include "core/lan_hotspot.hpp"
#include "core/lan_routing.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)p)
#define portEXIT_CRITICAL(p) ((void)p)
using esp_event_base_t=int;
#define ESP_EVENT_DEFINE_BASE(name) constexpr int name=9
constexpr int WIFI_EVENT=1,ESP_EVENT_ANY_ID=-1,WIFI_EVENT_AP_START=1,WIFI_EVENT_AP_STOP=2,
 WIFI_EVENT_AP_STACONNECTED=3,WIFI_EVENT_AP_STADISCONNECTED=4,WIFI_MODE_STA=1,WIFI_MODE_AP=2,WIFI_MODE_APSTA=3,
 WIFI_IF_AP=1,WIFI_STORAGE_RAM=0,WIFI_AUTH_WPA2_PSK=3,WIFI_BW20=0,WIFI_PS_NONE=0,
 WIFI_PROMIS_FILTER_MASK_DATA=4,ESP_WIFI_MAX_CONN_NUM=8,ESP_NETIF_OP_SET=1,ESP_NETIF_DOMAIN_NAME_SERVER=6,ESP_IPADDR_TYPE_V4=0,ESP_NETIF_DNS_MAIN=0;
struct esp_ip4_addr_t {uint32_t addr=0;};
struct esp_netif_ip_info_t {esp_ip4_addr_t ip,netmask,gw;};
struct esp_netif_dns_info_t {struct {int type=0;struct {esp_ip4_addr_t ip4;}u_addr;}ip;};
struct esp_netif_t {esp_netif_ip_info_t info{};uint32_t dns=0;bool attached=false,dhcp=false;};
struct esp_netif_config_t{};
#define ESP_NETIF_DEFAULT_WIFI_AP() esp_netif_config_t{}
struct wifi_init_config_t{};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t{struct{uint8_t ssid[32]{},password[64]{};size_t ssid_len=0;int channel=0,max_connection=0,authmode=0,beacon_interval=0;struct{bool required=false,capable=false;}pmf_cfg;}ap;};
struct wifi_sta_info_t{uint8_t mac[6]{};int rssi=0;};
struct wifi_sta_list_t{int num=0;wifi_sta_info_t sta[8];};
struct wifi_sta_mac_ip_list_t{int num=0;struct{uint8_t mac[6]{};esp_ip4_addr_t ip;}sta[8];};
using wifi_promiscuous_pkt_type_t=int;
constexpr int WIFI_PKT_DATA=2;
struct wifi_promiscuous_pkt_t{struct{int rx_state=0;size_t sig_len=0;}rx_ctrl;uint8_t payload[256]{};};
struct wifi_promiscuous_filter_t{int filter_mask=0;};
struct Handler{int base;void(*fn)(void*,int,int32_t,void*);};
using esp_event_handler_instance_t=Handler *;
static uint32_t htonl(uint32_t n){return ((n&255)<<24)|((n&0xff00)<<8)|((n>>8)&0xff00)|(n>>24);}
static uint32_t ntohl(uint32_t n){return htonl(n);}
namespace sys {struct HotspotUplink{uint32_t ip=0,netmask=0,gateway=0,dns=0;bool online=false;};}
namespace fake {
bool shared=false,reserved=false,driver=false,apOn=false,nat=false,sleep=false,dropStop=false;
unsigned init=0,deinit=0,driverStop=0,disconnects=0,enableNat=0,disableNat=0,stationRoute=0;
int64_t now=1000000;
std::string failure;unsigned failures=0;
std::vector<Handler*> handlers;esp_netif_t *netif=nullptr;sys::HotspotUplink up;
bool fault(const char *name){if(failure==name&&failures){--failures;return true;}return false;}
void emit(int base,int id){for(auto h:handlers)if(h->base==base)h->fn(nullptr,base,id,nullptr);}
}
static int64_t esp_timer_get_time(){return fake::now;}
static esp_err_t esp_wifi_init(const wifi_init_config_t*){CHECK(!fake::driver&&!fake::shared);if(fake::fault("init"))return ESP_ERR_NO_MEM;fake::driver=true;++fake::init;return ESP_OK;}
static esp_err_t esp_wifi_deinit(){CHECK(!fake::shared&&!fake::apOn);if(fake::fault("deinit"))return ESP_FAIL;fake::driver=false;++fake::deinit;return ESP_OK;}
static esp_netif_t *esp_netif_new(const esp_netif_config_t*){CHECK(!fake::netif);if(fake::fault("netif"))return nullptr;return fake::netif=new esp_netif_t;}
static esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *n){if(fake::fault("attach"))return ESP_FAIL;n->attached=true;return ESP_OK;}
static esp_err_t esp_wifi_set_default_wifi_ap_handlers(){return fake::fault("handlers")?ESP_FAIL:ESP_OK;}
static void esp_netif_destroy_default_wifi(esp_netif_t *n){CHECK(!fake::apOn&&!fake::nat);delete n;fake::netif=nullptr;}
static esp_err_t esp_event_handler_instance_register(int base,int,void(*fn)(void*,int,int32_t,void*),void*,Handler **out){if(fake::fault("register"))return ESP_FAIL;*out=new Handler{base,fn};fake::handlers.push_back(*out);return ESP_OK;}
static esp_err_t esp_event_handler_instance_unregister(int,int,Handler *h){auto i=std::find(fake::handlers.begin(),fake::handlers.end(),h);CHECK(i!=fake::handlers.end());fake::handlers.erase(i);delete h;return ESP_OK;}
static esp_err_t esp_event_post(int base,int id,void*,size_t,int){fake::emit(base,id);return ESP_OK;}
static esp_err_t esp_wifi_set_storage(int){return ESP_OK;}
static esp_err_t esp_wifi_set_mode(int mode){
 if(fake::fault("mode"))return ESP_FAIL;
 if(mode==WIFI_MODE_APSTA){CHECK(fake::shared&&fake::driver);fake::apOn=true;fake::netif->dhcp=true;fake::emit(WIFI_EVENT,WIFI_EVENT_AP_START);}
 if(mode==WIFI_MODE_STA){CHECK(fake::shared);fake::apOn=false;fake::netif->dhcp=false;if(!fake::dropStop)fake::emit(WIFI_EVENT,WIFI_EVENT_AP_STOP);}
 return ESP_OK;
}
static esp_err_t esp_wifi_set_config(int,const wifi_config_t *config){
 CHECK(config->ap.max_connection==4&&config->ap.authmode==WIFI_AUTH_WPA2_PSK);
 if(fake::fault("config"))return ESP_FAIL;
 // A live AP reconfigures SSID/security by emitting STOP then START. Only the
 // latest event describes its current state; STOP must not stay latched.
 if(fake::apOn){fake::emit(WIFI_EVENT,WIFI_EVENT_AP_STOP);fake::emit(WIFI_EVENT,WIFI_EVENT_AP_START);}
 return ESP_OK;
}
static esp_err_t esp_wifi_set_bandwidth(int,int){return fake::fault("bandwidth")?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_start(){if(fake::fault("start"))return ESP_FAIL;CHECK(!fake::shared);fake::apOn=true;fake::netif->dhcp=true;fake::emit(WIFI_EVENT,WIFI_EVENT_AP_START);return ESP_OK;}
static esp_err_t esp_wifi_stop(){CHECK(!fake::shared&&!fake::nat);if(fake::fault("stop"))return ESP_FAIL;++fake::driverStop;fake::apOn=false;fake::netif->dhcp=false;fake::emit(WIFI_EVENT,WIFI_EVENT_AP_STOP);return ESP_OK;}
static esp_err_t esp_wifi_set_max_tx_power(int){return ESP_OK;}
static esp_err_t esp_wifi_set_ps(int){return ESP_OK;}
static esp_err_t esp_wifi_set_inactive_time(int,int){return ESP_OK;}
static esp_err_t esp_wifi_deauth_sta(int id){CHECK(id==0);++fake::disconnects;return ESP_OK;}
static esp_err_t esp_wifi_get_mac(int,uint8_t *mac){std::memset(mac,1,6);return ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous(bool){return ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous_rx_cb(void(*)(void*,int)){return ESP_OK;}
static esp_err_t esp_wifi_set_tx_done_cb(void(*)(uint8_t,uint8_t*,uint16_t*,bool)){return ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t*){return ESP_OK;}
static esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *s){s->num=0;return ESP_OK;}
static esp_err_t esp_wifi_ap_get_sta_list_with_ip(const wifi_sta_list_t*,wifi_sta_mac_ip_list_t *s){s->num=0;return ESP_OK;}
static esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *out){*out=n->info;return ESP_OK;}
static void esp_ip4addr_ntoa(const esp_ip4_addr_t *ip,char *out,size_t size){auto h=ntohl(ip->addr);std::snprintf(out,size,"%u.%u.%u.%u",h>>24,(h>>16)&255,(h>>8)&255,h&255);}
static esp_err_t esp_netif_set_ip_info(esp_netif_t *n,const esp_netif_ip_info_t *info){CHECK(!n->dhcp);if(fake::fault("ip"))return ESP_FAIL;n->info=*info;return ESP_OK;}
static esp_err_t esp_netif_dhcps_stop(esp_netif_t *n){if(fake::fault("dhcp_stop"))return ESP_FAIL;const bool was=n->dhcp;n->dhcp=false;return was?ESP_OK:ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;}
static esp_err_t esp_netif_dhcps_start(esp_netif_t *n){if(fake::fault("dhcp_start"))return ESP_FAIL;CHECK(fake::apOn);n->dhcp=true;return ESP_OK;}
static esp_err_t esp_netif_dhcps_option(esp_netif_t *n,int,int,void *value,size_t size){CHECK(!n->dhcp&&size==1&&(*static_cast<uint8_t*>(value)==0||*static_cast<uint8_t*>(value)==2));return fake::fault("offer")?ESP_FAIL:ESP_OK;}
static esp_err_t esp_netif_set_dns_info(esp_netif_t *n,int,const esp_netif_dns_info_t *dns){if(fake::fault("dns"))return ESP_FAIL;n->dns=dns->ip.u_addr.ip4.addr;return ESP_OK;}
static esp_err_t esp_netif_napt_enable(esp_netif_t*){CHECK(fake::apOn&&fake::up.online&&fake::stationRoute);if(fake::fault("nat"))return ESP_FAIL;fake::nat=true;++fake::enableNat;return ESP_OK;}
static esp_err_t esp_netif_napt_disable(esp_netif_t*){CHECK(fake::nat);if(fake::fault("nat_off"))return ESP_FAIL;fake::nat=false;++fake::disableNat;return ESP_OK;}
namespace sys {
struct Connectivity{
 static bool hotspotReserved(){return fake::reserved;}
 static bool hotspotUsesStation(){return fake::shared;}
 static esp_err_t acquireForHotspot(){fake::reserved=true;return ESP_OK;}
 static void releaseFromHotspot(){CHECK(!fake::apOn&&!fake::netif&&!fake::nat);fake::reserved=false;}
 static HotspotUplink hotspotUplink(){return fake::shared?fake::up:HotspotUplink{};}
 static esp_err_t useStationRoute(){if(fake::fault("route"))return ESP_FAIL;CHECK(fake::shared&&fake::up.online);++fake::stationRoute;return ESP_OK;}
};
struct Audio{static bool releaseIdle(){return true;}};
struct SystemService{static void wake(){}static bool running(){return true;}};
struct Power{static bool sleepPending(){return fake::sleep;}};
struct Settings{static bool setLanCredentials(const char*,const char*){return true;}};
}
namespace sys::lanweb {
bool drain=false,on=false;unsigned starts=0;
void enable(uint32_t ip){CHECK(ip!=0);if(!on)++starts;on=true;}
void stop(){if(!drain)on=false;}
bool idle(){return !on;}
bool ready(){return on;}
esp_err_t error(){return ESP_OK;}
}
#include "lan_hotspot.inc"
static void tick(){fake::now+=1000000;sys::LanHotspot::poll();}
static void online(uint32_t ip=0xc0a80132,uint32_t mask=0xffffff00,uint32_t dns=0x08080808){fake::up={htonl(ip),htonl(mask),htonl((ip&mask)|1),htonl(dns),true};}
static uint32_t begin(bool shared,bool net=true){
 CHECK(sys::LanHotspot::idle());fake::shared=shared;fake::driver=shared;fake::up={};if(shared&&net)online();
 const auto id=sys::LanHotspot::open();CHECK(sys::LanHotspot::enable(id,"Yumo-LAN","12345678")==ESP_OK);tick();return id;
}
static void close(uint32_t id){sys::LanHotspot::close(id);for(unsigned i=0;i<10&&!sys::LanHotspot::idle();++i)tick();CHECK(sys::LanHotspot::idle()&&!fake::nat&&!fake::netif&&!fake::apOn&&fake::handlers.empty());CHECK(fake::driver==fake::shared);fake::driver=fake::shared=false;fake::sleep=false;}
int main(){
 using sys::LanHotspot;using sys::LanPhase;
 auto webLease=begin(false);CHECK(LanHotspot::snapshot().webReady);
 sys::lanweb::drain=true;LanHotspot::close(webLease);tick();
 CHECK(fake::apOn&&fake::netif&&!LanHotspot::idle());
 sys::lanweb::drain=false;close(webLease);CHECK(sys::lanweb::idle());
 for(uint32_t subnet=0;subnet<256;++subnet){const auto up=0xc0a80002u|(subnet<<8);auto ip=sys::lan::chooseAddress(up,0xffffff00);CHECK(ip&&!sys::lan::overlaps(ip,up,0xffffff00));}
 CHECK(sys::lan::chooseAddress(0xc0a80001,0xffff0000)==0xac1f4d01);
 auto id=begin(false);CHECK(LanHotspot::snapshot().phase==LanPhase::On&&!LanHotspot::snapshot().sharing&&fake::netif->dhcp);close(id);
 auto inits=fake::init,deinits=fake::deinit,stops=fake::driverStop;
 id=begin(true);CHECK(LanHotspot::snapshot().sharing&&fake::nat);CHECK(fake::init==inits&&fake::driverStop==stops);
 fake::up.online=false;tick();CHECK(!LanHotspot::snapshot().sharing&&fake::apOn&&fake::netif->dhcp&&!fake::nat);
 online();tick();CHECK(LanHotspot::snapshot().sharing);CHECK(fake::disconnects==0);
 online(0xc0a80464);tick();CHECK(LanHotspot::snapshot().sharing&&ntohl(fake::netif->info.ip.addr)!=0xc0a80401);CHECK(fake::disconnects==1);
 online(0xc0a80464,0xffffff00,0x01010101);tick();CHECK(fake::netif->dns==htonl(0x01010101)&&fake::disconnects==2);
 close(id);CHECK(fake::deinit==deinits&&fake::driverStop==stops);
 id=begin(true,false);CHECK(!LanHotspot::snapshot().sharing&&fake::apOn);online();tick();CHECK(LanHotspot::snapshot().sharing);close(id);
 for(const char *fault: {"init","netif","attach","handlers","register","ip","offer","mode","config","bandwidth","start"}){
  fake::failure=fault;fake::failures=1;id=begin(false);close(id);CHECK(!fake::failures);fake::failure.clear();
 }
 for(const char *fault:{"netif","attach","handlers","register","ip","offer","dns","mode","config","bandwidth"}){
  fake::failure=fault;fake::failures=1;id=begin(true);close(id);CHECK(!fake::failures);fake::failure.clear();
 }
 for(const char *fault:{"nat","route","dns","dhcp_start","dhcp_stop","ip","offer"}){
  id=begin(true,false);fake::failure=fault;fake::failures=1;online();tick();CHECK(!LanHotspot::snapshot().sharing);CHECK(fake::apOn);
  fake::failure.clear();tick();CHECK(LanHotspot::snapshot().sharing);close(id);
 }
 id=begin(true);fake::failure="nat_off";fake::failures=1;fake::up.online=false;tick();CHECK(!LanHotspot::snapshot().sharing);tick();CHECK(!fake::nat&&fake::apOn);close(id);fake::failure.clear();
 id=begin(true);fake::dropStop=true;LanHotspot::close(id);for(int i=0;i<8;++i)tick();CHECK(!LanHotspot::idle()&&fake::driver);
 fake::dropStop=false;fake::emit(WIFI_EVENT,WIFI_EVENT_AP_STOP);tick();close(id);
 id=begin(true);fake::sleep=true;tick();close(id);
 auto old=LanHotspot::open();auto next=LanHotspot::open();LanHotspot::close(old);CHECK(LanHotspot::enable(old,"Yumo-LAN","12345678")!=ESP_OK);CHECK(LanHotspot::enable(next,"Yumo-LAN","12345678")==ESP_OK);tick();close(next);
 std::printf("PASS: %u production LAN sharing checks: offline, uplink preservation, loss/recovery, DNS/subnet changes, faults and teardown\n",checks);
}
