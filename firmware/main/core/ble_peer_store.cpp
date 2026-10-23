#include "ble_peer_store.hpp"
#include "nvs.h"
#include "esp_log.h"
#include <array>
#include <cstdio>
#include <mutex>
#include <cstring>
namespace sys {
namespace {
std::mutex mutex;
struct Record { uint8_t type=0,address[6]{};bool keyboard=false;char name[64]{}; };
struct Saved { uint32_t version=1;std::array<Record,8> records{};Record target; } saved;
BleBond current;
uint32_t revision=0,flushed=0;
Record record(const BleBond &v) { Record r;r.type=v.addrType;std::memcpy(r.address,v.addr,6);r.keyboard=v.keyboard;std::memcpy(r.name,v.name,sizeof(r.name));r.name[63]=0;return r; }
BleBond bond(const Record &r) { BleBond b;b.addrType=r.type;std::memcpy(b.addr,r.address,6);b.keyboard=r.keyboard;std::memcpy(b.name,r.name,sizeof(b.name));b.name[63]=0;return b; }
bool empty(const uint8_t *a) { for(int i=0;i<6;++i) if(a[i]) return false;return true; }
}
bool BlePeerStore::same(const BleBond &a,const BleBond &b) { return (a.addrType&1)==(b.addrType&1) && std::memcmp(a.addr,b.addr,6)==0; }
void BlePeerStore::load() {
    nvs_handle_t h;Saved value;size_t length=sizeof(value);
    if(nvs_open("ble_peers",NVS_READONLY,&h)!=ESP_OK) return;
    auto err=nvs_get_blob(h,"v1",&value,&length);nvs_close(h);
    if(err!=ESP_OK || length!=sizeof(value) || value.version!=1) return;
    for(auto &r:value.records) r.name[63]=0;
    value.target.name[63]=0;
    const std::lock_guard<std::mutex> lock(mutex);saved=value;
}
void BlePeerStore::flush() {
    Saved copy;uint32_t gen;
    { const std::lock_guard<std::mutex> lock(mutex);if(revision==flushed) return;copy=saved;gen=revision; }
    nvs_handle_t h;auto err=nvs_open("ble_peers",NVS_READWRITE,&h);
    if(err==ESP_OK) { err=nvs_set_blob(h,"v1",&copy,sizeof(copy));if(err==ESP_OK) err=nvs_commit(h);nvs_close(h); }
    if(err==ESP_OK) { const std::lock_guard<std::mutex> lock(mutex);flushed=gen; }
    else ESP_LOGW("ble_peers","Cannot save device names: %s",esp_err_to_name(err));
}
BleBond BlePeerStore::enrich(BleBond value) {
    const std::lock_guard<std::mutex> lock(mutex);
    for(const auto &r:saved.records) if(!empty(r.address) && same(value,bond(r))) {
        if(!value.name[0]) std::memcpy(value.name,r.name,sizeof(value.name));
        value.keyboard=r.keyboard;break;
    }
    return value;
}
void BlePeerStore::remember(const BleBond &value) {
    if(empty(value.addr)) return;
    const std::lock_guard<std::mutex> lock(mutex);
    Record *slot=nullptr;
    for(auto &r:saved.records) { if(same(value,bond(r))) { slot=&r;break; } if(!slot && empty(r.address)) slot=&r; }
    if(!slot) return; // never evict unrelated metadata implicitly
    auto next=record(value);if(!next.name[0]) std::memcpy(next.name,slot->name,sizeof(next.name));
    if(std::memcmp(slot,&next,sizeof(next))) { *slot=next;++revision; }
}
void BlePeerStore::forget(const BleBond &value) {
    const std::lock_guard<std::mutex> lock(mutex);
    for(auto &r:saved.records) if(same(value,bond(r))) { r={};++revision; }
    if(same(value,bond(saved.target))) { saved.target={};++revision; }
}
BleBond BlePeerStore::target() { const std::lock_guard<std::mutex> lock(mutex);return bond(saved.target); }
void BlePeerStore::setTarget(const BleBond &value) {
    const auto next=record(value);const std::lock_guard<std::mutex> lock(mutex);
    if(std::memcmp(&next,&saved.target,sizeof(next))) { saved.target=next;++revision; }
}
void BlePeerStore::activate(const BleBond &value) { const std::lock_guard<std::mutex> lock(mutex);current=value; }
BleBond BlePeerStore::active() { const std::lock_guard<std::mutex> lock(mutex);return current; }
void BlePeerStore::name(const char *value) {
    if(!value || !value[0]) return;
    BleBond peer;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        // Input devices can advertise distinct mode names but return one generic
        // GAP name. Keep the selected mode label for this address and reconnect.
        if(current.keyboard && current.name[0]) return;
        std::snprintf(current.name,sizeof(current.name),"%s",value);peer=current;
    }
    remember(peer);
    if(same(peer,target())) setTarget(peer);
}
void BlePeerStore::clearActive() { const std::lock_guard<std::mutex> lock(mutex);current={}; }
}
