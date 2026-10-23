#include "lan_hotspot.hpp"
#include "lan_web.hpp"
#include "lan_routing.hpp"
#include "connectivity.hpp"
#include "system_service.hpp"
#include "settings_store.hpp"
#include "audio_mgr.hpp"
#include "power_mgr.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "esp_private/wifi.h" // IDF TxDone adapter; isolated here, never in App/UI
#include "esp_timer.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace sys {
namespace {
ESP_EVENT_DEFINE_BASE(YUMO_LAN_FENCE);
struct Request { uint32_t session=0; bool open=false,wanted=false; char ssid[33]{},password[64]{}; };
std::mutex mailbox;
Request request;
LanSnapshot published;
LanSnapshot current; // SystemService owns all runtime state below
uint32_t activeSession=0;
esp_netif_t *ap=nullptr;
esp_event_handler_instance_t events=nullptr;
esp_event_handler_instance_t fenceEvents=nullptr;
std::atomic<bool> fenceReached{false};
bool fencePosted=false,recovering=false;
bool driver=false,radio=false,stopIssued=false,meterArmed=false;
bool borrowed=false,napt=false;
uint32_t apAddress=0,dnsAddress=0,upstreamAddress=0,upstreamGateway=0;
int64_t nextRoute=0;
int64_t deadline=0,retryAt=0,nextSample=0,lastSample=0;
std::atomic<bool> started{false},stopped{false},clientsChanged{false};
// Wi-Fi callbacks retain only static, bounded bookkeeping, never an App,
// netif, heap packet or LVGL pointer. This also makes late callbacks harmless.
portMUX_TYPE meterMux=portMUX_INITIALIZER_UNLOCKED;
lan::Meter meter;
bool meterEnabled=false;
struct Previous { uint8_t mac[6]{}; uint64_t up=0,down=0; };
std::array<Previous,lan::MaxClients> previous{};

void publish() { const std::lock_guard<std::mutex> lock(mailbox); published=current; }
void fail(esp_err_t err) {
    current.error=err;
    { const std::lock_guard<std::mutex> lock(mailbox);
      if(request.session==activeSession) request.wanted=false; }
    current.phase=LanPhase::Stopping;
    deadline=esp_timer_get_time()+lan::TransitionUs;
}
void radioEvent(void *,esp_event_base_t base,int32_t id,void *) {
    if(base==YUMO_LAN_FENCE) { fenceReached.store(true); return; }
    // Configuring an AP on an already-started STA can restart only the AP.
    // Event-loop order is authoritative: a subsequent START supersedes STOP.
    if(id==WIFI_EVENT_AP_START) {stopped.store(false);started.store(true);}
    else if(id==WIFI_EVENT_AP_STOP) {started.store(false);stopped.store(true);}
    else if(id==WIFI_EVENT_AP_STACONNECTED || id==WIFI_EVENT_AP_STADISCONNECTED) clientsChanged.store(true);
}
void receive(void *buffer,wifi_promiscuous_pkt_type_t type) {
    if(!buffer || type!=WIFI_PKT_DATA) return;
    const auto *packet=static_cast<const wifi_promiscuous_pkt_t *>(buffer);
    if(packet->rx_ctrl.rx_state!=0) return;
    portENTER_CRITICAL(&meterMux);
    if(meterEnabled) meter.uplink(packet->payload,packet->rx_ctrl.sig_len);
    portEXIT_CRITICAL(&meterMux);
}
void transmitted(uint8_t iface,uint8_t *data,uint16_t *length,bool success) {
    if(iface!=WIFI_IF_AP || !success || !data || !length) return;
    portENTER_CRITICAL(&meterMux);
    if(meterEnabled) meter.downlink(data,*length);
    portEXIT_CRITICAL(&meterMux);
}
void stopMeter() {
    portENTER_CRITICAL(&meterMux); meterEnabled=false; portEXIT_CRITICAL(&meterMux);
    if((driver || borrowed) && meterArmed) {
        meterArmed=false;
        (void)esp_wifi_set_promiscuous(false);
        (void)esp_wifi_set_promiscuous_rx_cb(nullptr);
        (void)esp_wifi_set_tx_done_cb(nullptr);
    }
    current.uploadAvailable=current.downloadAvailable=false;
}
void startMeter() {
    uint8_t mac[6]{};
    if(esp_wifi_get_mac(WIFI_IF_AP,mac)!=ESP_OK) return;
    portENTER_CRITICAL(&meterMux);
    meter={}; std::memcpy(meter.bssid,mac,6); meterEnabled=true;
    portEXIT_CRITICAL(&meterMux);
    meterArmed=true;
    wifi_promiscuous_filter_t filter{}; filter.filter_mask=WIFI_PROMIS_FILTER_MASK_DATA;
    current.uploadAvailable=esp_wifi_set_promiscuous_filter(&filter)==ESP_OK &&
        esp_wifi_set_promiscuous_rx_cb(receive)==ESP_OK && esp_wifi_set_promiscuous(true)==ESP_OK;
    if(!current.uploadAvailable) {
        (void)esp_wifi_set_promiscuous(false); (void)esp_wifi_set_promiscuous_rx_cb(nullptr);
    }
    current.downloadAvailable=esp_wifi_set_tx_done_cb(transmitted)==ESP_OK;
    // These are observed radio-byte estimates, NOT application/file goodput.
    // Firmware-internal forwarding and aggregate frame coverage need device tests.
}
esp_err_t stopForwarding() {
    current.sharing=false;
    if(!napt)return ESP_OK;
    const auto err=esp_netif_napt_disable(ap);
    if(err==ESP_OK)napt=false;
    return err;
}
esp_err_t configureNetwork(uint32_t address,uint32_t dns,bool running) {
    auto err=esp_netif_dhcps_stop(ap);
    if(err!=ESP_OK && err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)return err;
    esp_netif_ip_info_t info{};info.ip.addr=address;info.gw.addr=address;info.netmask.addr=htonl(lan::Mask);
    err=esp_netif_set_ip_info(ap,&info);
    uint8_t offer=dns?2:0;
    if(err==ESP_OK)err=esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer,sizeof(offer));
    if(err==ESP_OK && dns) {
        esp_netif_dns_info_t value{};value.ip.type=ESP_IPADDR_TYPE_V4;value.ip.u_addr.ip4.addr=dns;
        err=esp_netif_set_dns_info(ap,ESP_NETIF_DNS_MAIN,&value);
    }
    if(running) {
        const auto start=esp_netif_dhcps_start(ap);
        if(err==ESP_OK && start!=ESP_OK && start!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)err=start;
    }
    if(err==ESP_OK) {
        apAddress=address;dnsAddress=dns;
        esp_ip4addr_ntoa(&info.ip,current.ip,sizeof(current.ip));
    }
    return err;
}
void refreshSharing(int64_t now) {
    if(now<nextRoute)return;
    nextRoute=now+1000000;
    const auto up=Connectivity::hotspotUplink();
    current.upstreamConnected=up.online;
    current.sharingError=ESP_OK;
    if(!up.online) {
        current.sharingError=stopForwarding();
        upstreamAddress=upstreamGateway=0;
        return; // Keep local DHCP/associations and last DNS lease during a dropout.
    }
    const uint32_t selected=lan::chooseAddress(ntohl(up.ip),ntohl(up.netmask),ntohl(apAddress));
    if(!selected) { (void)stopForwarding();current.sharingError=ESP_ERR_INVALID_STATE;return; }
    // The router's DNS normally arrives through DHCP. Do not invent a successful
    // resolver when the upstream has not supplied one; retry on the next tick.
    if(!up.dns) {(void)stopForwarding();current.sharingError=ESP_ERR_NOT_FOUND;return;}
    const auto address=htonl(selected);
    const bool changed=address!=apAddress || dnsAddress!=up.dns;
    if(changed || up.ip!=upstreamAddress || up.gateway!=upstreamGateway) {
        const auto err=stopForwarding();if(err!=ESP_OK){current.sharingError=err;return;}
    }
    if(changed) {
        const auto err=configureNetwork(address,up.dns,true);
        if(err!=ESP_OK){current.sharingError=err;return;}
        // New subnet/DNS needs a fresh lease. Existing clients reconnect rather
        // than silently retaining an unusable address until lease expiry.
        (void)esp_wifi_deauth_sta(0);
        current.count=0;current.clients={};previous={};lastSample=0;
    }
    auto err=Connectivity::useStationRoute();
    if(err==ESP_OK && !napt) {err=esp_netif_napt_enable(ap);napt=err==ESP_OK;}
    current.sharing=err==ESP_OK && napt;current.sharingError=err;
    if(current.sharing){upstreamAddress=up.ip;upstreamGateway=up.gateway;}
}
esp_err_t startRadio(const Request &intent) {
    (void)Audio::releaseIdle();
    borrowed=Connectivity::hotspotUsesStation();
    esp_err_t err=ESP_OK;
    if(!borrowed) {
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
        err=esp_wifi_init(&init);driver=err==ESP_OK;
    }
    if(err!=ESP_OK) return err;
    esp_netif_config_t config=ESP_NETIF_DEFAULT_WIFI_AP();
    // Do not use the assert/abort-based create_default_wifi_ap convenience API.
    ap=esp_netif_new(&config);
    if(!ap) return ESP_ERR_NO_MEM;
    err=esp_netif_attach_wifi_ap(ap);
    if(err==ESP_OK) err=esp_wifi_set_default_wifi_ap_handlers();
    if(err==ESP_OK) err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,radioEvent,nullptr,&events);
    if(err==ESP_OK) err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    const auto up=Connectivity::hotspotUplink();
    const auto address=lan::chooseAddress(ntohl(up.ip),ntohl(up.netmask));
    if(err==ESP_OK && !address)err=ESP_ERR_INVALID_STATE;
    if(err==ESP_OK)err=configureNetwork(htonl(address),up.online?up.dns:0,false);
    if(err==ESP_OK) {
        err=esp_wifi_set_mode(borrowed?WIFI_MODE_APSTA:WIFI_MODE_AP);
        // Adding AP to a running STA starts it without restarting the driver.
        if(borrowed && err==ESP_OK)radio=true;
    }
    wifi_config_t wifi{};
    const size_t nameLength=std::strlen(intent.ssid);
    std::memcpy(wifi.ap.ssid,intent.ssid,nameLength); wifi.ap.ssid_len=nameLength;
    std::memcpy(wifi.ap.password,intent.password,std::strlen(intent.password));
    wifi.ap.channel=6; wifi.ap.max_connection=lan::MaxClients;
    wifi.ap.authmode=WIFI_AUTH_WPA2_PSK;
    wifi.ap.pmf_cfg.required=false; wifi.ap.pmf_cfg.capable=true;
    wifi.ap.beacon_interval=100;
    if(err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_AP,&wifi);
    std::memset(wifi.ap.password,0,sizeof(wifi.ap.password));
    if(err==ESP_OK) err=esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW20);
    if(err==ESP_OK && !borrowed) { err=esp_wifi_start(); radio=err==ESP_OK; }
    if(err==ESP_OK) {
        (void)esp_wifi_set_max_tx_power(52); // same supply/brownout limit as STA
        (void)esp_wifi_set_ps(WIFI_PS_NONE);
        (void)esp_wifi_set_inactive_time(WIFI_IF_AP,300);
    }
    return err;
}
bool cleanup() {
    lanweb::stop();
    if(!lanweb::idle())return false;
    stopMeter();
    if(stopForwarding()!=ESP_OK)return false;
    if(radio && !stopped.load()) {
        const auto now=esp_timer_get_time();
        if(!stopIssued && now>=retryAt) {
            const auto err=borrowed?esp_wifi_set_mode(WIFI_MODE_STA):esp_wifi_stop();
            if(err==ESP_OK) stopIssued=true;
            else if(err==ESP_ERR_WIFI_NOT_STARTED) { stopIssued=true; stopped.store(true); }
            else { current.error=err; retryAt=now+1000000; }
        }
        if(!stopped.load()) {
            if(!deadline || now<deadline || now<retryAt) return false;
            current.error=ESP_ERR_TIMEOUT;
            retryAt=now+1000000;
            if(borrowed) {
                stopIssued=false; // Retry AP removal; never deinit the borrowed STA.
                return false;
            }
            // Driver teardown is the only substitute for a missing STOP event.
            // Never release the netif/radio owner while teardown still fails.
            const auto err=esp_wifi_deinit();
            if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_INIT) return false;
            driver=false;radio=false;recovering=true;
        }
    }
    if(recovering) {
        if(!fenceEvents && esp_event_handler_instance_register(YUMO_LAN_FENCE,0,radioEvent,nullptr,&fenceEvents)!=ESP_OK)
            return false;
        if(!fencePosted) {
            fenceReached.store(false);
            fencePosted=esp_event_post(YUMO_LAN_FENCE,0,nullptr,0,0)==ESP_OK;
            return false;
        }
        if(!fenceReached.load()) return false; // Drain old AP events before a new lease can start.
    }
    radio=false;
    if(driver) {
        const auto err=esp_wifi_deinit();
        if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_INIT) { current.error=err; return false; }
        driver=false;
    }
    if(events) {
        if(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,events)!=ESP_OK)return false;
        events=nullptr;
    }
    if(fenceEvents) {
        if(esp_event_handler_instance_unregister(YUMO_LAN_FENCE,0,fenceEvents)!=ESP_OK)return false;
        fenceEvents=nullptr;
    }
    if(ap) { esp_netif_destroy_default_wifi(ap); ap=nullptr; }
    Connectivity::releaseFromHotspot();
    borrowed=false;napt=false;apAddress=dnsAddress=upstreamAddress=upstreamGateway=0;nextRoute=0;
    portENTER_CRITICAL(&meterMux); meter={}; portEXIT_CRITICAL(&meterMux);
    previous={}; started.store(false); stopped.store(false); clientsChanged.store(false);
    stopIssued=false; deadline=retryAt=nextSample=lastSample=0;
    recovering=fencePosted=false;fenceReached.store(false);
    const auto error=current.error; current={}; current.error=error;
    current.phase=error==ESP_OK?LanPhase::Off:LanPhase::Failed;
    return true;
}
void sample(int64_t now) {
    wifi_sta_list_t list{};
    if(esp_wifi_ap_get_sta_list(&list)!=ESP_OK) {
        current.count=0; current.clients={}; previous={}; lastSample=now; return;
    }
    const unsigned count=std::min<unsigned>(std::max<int>(0,list.num),lan::MaxClients);
    std::sort(list.sta,list.sta+count,[](const wifi_sta_info_t &a,const wifi_sta_info_t &b){return std::memcmp(a.mac,b.mac,6)<0;});
    wifi_sta_mac_ip_list_t addresses{};
    (void)esp_wifi_ap_get_sta_list_with_ip(&list,&addresses);
    std::array<lan::Counter,lan::MaxClients> counters{};
    portENTER_CRITICAL(&meterMux);
    for(unsigned i=0;i<count;++i) {
        const int old=meter.find(list.sta[i].mac);
        if(old>=0) counters[i]=meter.clients[old];
        counters[i].used=true; std::memcpy(counters[i].mac,list.sta[i].mac,6);
    }
    meter.clients=counters;
    portEXIT_CRITICAL(&meterMux);
    std::array<Previous,lan::MaxClients> nextPrevious{};
    current.count=count; current.clients={};
    for(unsigned i=0;i<count;++i) {
        auto &peer=current.clients[i]; const auto &counter=counters[i];
        std::memcpy(peer.mac,counter.mac,6); peer.rssi=list.sta[i].rssi;
        for(int j=0;j<addresses.num && j<ESP_WIFI_MAX_CONN_NUM;++j)
            if(lan::sameMac(peer.mac,addresses.sta[j].mac) && addresses.sta[j].ip.addr)
                esp_ip4addr_ntoa(&addresses.sta[j].ip,peer.ip,sizeof(peer.ip));
        if(lastSample) for(const auto &old:previous) if(lan::sameMac(old.mac,peer.mac)) {
            peer.uploadBps=lan::rate(counter.upload-old.up,now-lastSample);
            peer.downloadBps=lan::rate(counter.download-old.down,now-lastSample); break;
        }
        std::memcpy(nextPrevious[i].mac,peer.mac,6);
        nextPrevious[i].up=counter.upload; nextPrevious[i].down=counter.download;
    }
    previous=nextPrevious; lastSample=now;
}
} // namespace

uint32_t LanHotspot::open() {
    const std::lock_guard<std::mutex> lock(mailbox);
    ++request.session; if(!request.session) ++request.session;
    request.open=true; request.wanted=false;
    std::memset(request.password,0,sizeof(request.password));
    return request.session;
}
void LanHotspot::close(uint32_t session) {
    const std::lock_guard<std::mutex> lock(mailbox);
    if(!session || request.session!=session) return;
    request.open=false; request.wanted=false;
    std::memset(request.password,0,sizeof(request.password));
}
esp_err_t LanHotspot::enable(uint32_t session,const std::string &ssid,const std::string &password) {
    if(lan::validate(ssid,password)!=lan::ConfigError::None) return ESP_ERR_INVALID_ARG;
    if(!SystemService::running() || Power::sleepPending()) return ESP_ERR_INVALID_STATE;
    const std::lock_guard<std::mutex> lock(mailbox);
    if(!session || session!=request.session || !request.open) return ESP_ERR_INVALID_STATE;
    if(published.busy() || published.phase==LanPhase::On || request.wanted) return ESP_ERR_NOT_FINISHED;
    std::memset(request.ssid,0,sizeof(request.ssid)); std::memset(request.password,0,sizeof(request.password));
    std::memcpy(request.ssid,ssid.data(),ssid.size()); std::memcpy(request.password,password.data(),password.size());
    request.wanted=true;
    return ESP_OK;
}
void LanHotspot::disable(uint32_t session) {
    const std::lock_guard<std::mutex> lock(mailbox);
    if(session && session==request.session) { request.wanted=false; std::memset(request.password,0,sizeof(request.password)); }
}
LanSnapshot LanHotspot::snapshot() {
    const std::lock_guard<std::mutex> lock(mailbox);
    auto out=published; out.wanted=request.open && request.wanted;
    if((out.phase==LanPhase::Off || out.phase==LanPhase::Failed) && out.wanted) out.phase=LanPhase::Preparing;
    if(out.phase==LanPhase::On && !out.wanted) out.phase=LanPhase::Stopping;
    return out;
}
bool LanHotspot::idle() {
    const std::lock_guard<std::mutex> lock(mailbox);
    return !request.wanted && !Connectivity::hotspotReserved() &&
        (published.phase==LanPhase::Off || published.phase==LanPhase::Failed);
}
void LanHotspot::poll() {
    Request intent;
    { const std::lock_guard<std::mutex> lock(mailbox);
      if(Power::sleepPending()) { request.wanted=false; std::memset(request.password,0,sizeof(request.password)); }
      intent=request; }
    const auto now=esp_timer_get_time();
    if(current.phase==LanPhase::Off || current.phase==LanPhase::Failed) {
        if(!intent.open || !intent.wanted) return;
        activeSession=intent.session; current={}; current.phase=LanPhase::Preparing;
        deadline=now+lan::TransitionUs;
    }
    if((!intent.open || !intent.wanted || intent.session!=activeSession) && current.phase!=LanPhase::Stopping) {
        current.phase=LanPhase::Stopping; deadline=now+lan::TransitionUs;
    }
    if(current.phase==LanPhase::Preparing) {
        const auto err=Connectivity::acquireForHotspot();
        if(err==ESP_ERR_NOT_FINISHED) { if(now>=deadline) fail(ESP_ERR_TIMEOUT); }
        else if(err!=ESP_OK) fail(err);
        else {
            const auto startError=startRadio(intent);
            if(startError!=ESP_OK) fail(startError);
            else { current.phase=LanPhase::Starting; deadline=now+lan::TransitionUs; }
        }
    }
    if(current.phase==LanPhase::Starting) {
        if(started.load()) {
            current.phase=LanPhase::On; deadline=0;
            esp_netif_ip_info_t ip{};
            if(esp_netif_get_ip_info(ap,&ip)==ESP_OK) esp_ip4addr_ntoa(&ip.ip,current.ip,sizeof(current.ip));
            startMeter(); nextSample=0;
            nextRoute=0;
            // Persist only a validated configuration that actually reached AP_START.
            (void)Settings::setLanCredentials(intent.ssid,intent.password);
        } else if(now>=deadline) fail(ESP_ERR_TIMEOUT);
    }
    if(current.phase==LanPhase::On) {
        refreshSharing(now);
        lanweb::enable(apAddress);
        current.webReady=lanweb::ready();
        current.webError=lanweb::error();
        if(stopped.load()) fail(ESP_ERR_WIFI_NOT_STARTED);
        else if(now>=nextSample || clientsChanged.exchange(false)) { nextSample=now+lan::SampleUs; sample(now); }
    }
    if(current.phase==LanPhase::Stopping) (void)cleanup();
    std::memset(intent.password,0,sizeof(intent.password));
    publish();
}
} // namespace sys
