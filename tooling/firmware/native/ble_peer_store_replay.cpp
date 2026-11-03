// Synthetic identities and in-memory NVS only; no real bonds or keys are read.
#include "core/ble_peer_store.hpp"
#include "core/system_service.hpp"
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>
static unsigned checks=0,scenarios=0,commits=0,writes=0,opens=0,closes=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define ESP_LOGW(...)
using nvs_handle_t=int;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
static std::vector<uint8_t> disk,pending;
static bool failOpen=false,failQuery=false,failRead=false,failSet=false,failCommit=false,commitThenFail=false;
static std::function<void()> commitHook;
static int nvs_open(const char *name,int,nvs_handle_t *h){CHECK(!std::strcmp(name,"ble_peers"));if(failOpen)return 1;++opens;*h=1;return 0;}
static void nvs_close(nvs_handle_t){++closes;pending.clear();}
static int nvs_get_blob(nvs_handle_t,const char *key,void *value,size_t *length){
    CHECK(!std::strcmp(key,"v1"));if(disk.empty())return 1;
    if(!value){if(failQuery)return 1;*length=disk.size();return 0;}
    if(failRead||*length<disk.size())return 1;*length=disk.size();std::memcpy(value,disk.data(),disk.size());return 0;
}
static int nvs_set_blob(nvs_handle_t,const char *key,const void *value,size_t length){
    CHECK(!std::strcmp(key,"v1"));++writes;if(failSet)return 1;
    auto p=static_cast<const uint8_t *>(value);pending.assign(p,p+length);return 0;
}
static int nvs_commit(nvs_handle_t){
    ++commits;if(failCommit)return 1;disk=pending;
    if(commitHook){auto hook=std::move(commitHook);commitHook={};hook();}
    return commitThenFail?1:0;
}
#include "peer_store.inc"
// Independent declaration of the old firmware's persisted ABI.
struct LegacyRecord {uint8_t type=0,address[6]{};bool keyboard=false;char name[64]{};};
struct LegacySaved {uint32_t version=1;std::array<LegacyRecord,8> records{};LegacyRecord target;};
static_assert(sizeof(LegacyRecord)==72 && sizeof(LegacySaved)==652);
template<class T>std::vector<uint8_t> bytes(const T &value){const auto p=reinterpret_cast<const uint8_t *>(&value);return {p,p+sizeof(value)};}
void reset(){
    CHECK(opens==closes);sys::saved={};sys::current={};sys::revision=sys::flushed=0;
    failOpen=failQuery=failRead=failSet=failCommit=commitThenFail=false;
    commitHook={};pending.clear();commits=writes=opens=closes=0;
}
using sys::BlePeerStore;
std::array<sys::BleBond,16> peers;
void checkPeer(unsigned i,bool present=true){
    auto bare=peers[i];bare.name[0]=0;bare.keyboard=!peers[i].keyboard;
    const auto restored=BlePeerStore::enrich(bare);CHECK((restored.name[0]!=0)==present);
    if(present){CHECK(!std::strcmp(restored.name,peers[i].name));CHECK(restored.keyboard==peers[i].keyboard);}
}
void checkEmpty(){for(const auto &r:sys::saved.records)CHECK(!r.address[0]);CHECK(!BlePeerStore::target().addr[0]);CHECK(!sys::revision);}
int main(){
    static_assert(std::tuple_size<decltype(sys::BluetoothSnapshot{}.bonds)>::value==16);
    static_assert(BlePeerStore::Capacity==16);CHECK(sys::saved.records.size()==16);
    const char *modes[]={"IINE-keyboard","Xbox Wireless Controller","Wireless Controller","IINE-keyboard"};
    for(unsigned i=0;i<peers.size();++i){
        auto &p=peers[i];p.addrType=1;p.addr[0]=uint8_t(i+1);p.addr[5]=0xc0;p.keyboard=i%4!=3;
        if(i<4)std::snprintf(p.name,sizeof(p.name),"%s",modes[i]);else std::snprintf(p.name,sizeof(p.name),"Synthetic peer %u",i+1);
    }
    LegacySaved legacy;
    for(unsigned i=0;i<8;++i){auto &r=legacy.records[i];const auto &p=peers[i];r.type=p.addrType;std::memcpy(r.address,p.addr,6);r.keyboard=p.keyboard;std::memcpy(r.name,p.name,64);}
    legacy.target=legacy.records[1];const auto original=bytes(legacy);
    ++scenarios;disk=original;reset();BlePeerStore::load();
    CHECK(disk==original&&writes==0);CHECK(BlePeerStore::same(BlePeerStore::target(),peers[1]));CHECK(!std::strcmp(BlePeerStore::target().name,peers[1].name));
    for(unsigned i=0;i<16;++i)checkPeer(i,i<8);
    CHECK(sys::revision!=sys::flushed);BlePeerStore::flush();CHECK(commits==1&&writes==1&&disk.size()==1228);CHECK(sys::flushed==sys::revision);
    uint32_t version=0;std::memcpy(&version,disk.data(),4);CHECK(version==2);const auto migrated=disk;
    ++scenarios;reset();BlePeerStore::load();for(unsigned i=0;i<16;++i)checkPeer(i,i<8);BlePeerStore::flush();CHECK(!writes&&disk==migrated);
    ++scenarios;for(unsigned i=8;i<16;++i)BlePeerStore::remember(peers[i]);BlePeerStore::setTarget(peers[15]);BlePeerStore::flush();
    const auto all=disk;CHECK(commits==1);reset();BlePeerStore::load();for(unsigned i=0;i<16;++i)checkPeer(i);CHECK(BlePeerStore::same(BlePeerStore::target(),peers[15]));
    ++scenarios;for(unsigned i=0;i<16;++i)for(unsigned j=0;j<16;++j)CHECK(BlePeerStore::same(peers[i],peers[j])==(i==j));
    auto resolved=peers[0];resolved.addrType=3;CHECK(BlePeerStore::same(resolved,peers[0]));resolved.name[0]=0;CHECK(!std::strcmp(BlePeerStore::enrich(resolved).name,peers[0].name));
    auto publicPeer=peers[0];publicPeer.addrType=0;CHECK(!BlePeerStore::same(publicPeer,peers[0]));
    ++scenarios;auto seventeenth=peers[0];seventeenth.addr[0]=17;BlePeerStore::remember(seventeenth);BlePeerStore::flush();CHECK(disk==all&&!writes);for(unsigned i=0;i<16;++i)checkPeer(i);
    ++scenarios;for(unsigned i=0;i<3;++i){BlePeerStore::activate(peers[i]);BlePeerStore::name("HID Keyboard");CHECK(!std::strcmp(BlePeerStore::active().name,peers[i].name));}BlePeerStore::flush();CHECK(disk==all&&!writes);
    ++scenarios;auto unnamed=peers[4];unnamed.name[0]=0;BlePeerStore::remember(unnamed);BlePeerStore::flush();CHECK(disk==all&&!writes);
    ++scenarios;BlePeerStore::forget(peers[15]);BlePeerStore::flush();CHECK(!BlePeerStore::target().addr[0]);reset();BlePeerStore::load();for(unsigned i=0;i<16;++i)checkPeer(i,i!=15);
    BlePeerStore::remember(seventeenth);BlePeerStore::flush();reset();BlePeerStore::load();auto blank=seventeenth;blank.name[0]=0;CHECK(!std::strcmp(BlePeerStore::enrich(blank).name,seventeenth.name));
    ++scenarios;auto phone=peers[3];BlePeerStore::activate(phone);BlePeerStore::setTarget(phone);BlePeerStore::name("New phone name");CHECK(!std::strcmp(BlePeerStore::active().name,"New phone name"));CHECK(!std::strcmp(BlePeerStore::target().name,"New phone name"));
    ++scenarios;auto keyboard=peers[4];keyboard.name[0]=0;BlePeerStore::activate(keyboard);BlePeerStore::name("New keyboard name");CHECK(!std::strcmp(BlePeerStore::active().name,"New keyboard name"));BlePeerStore::clearActive();CHECK(!BlePeerStore::active().addr[0]);
    // Failed migration stays dirty for retry, including an ambiguous commit
    // failure after the new blob reached NVS. Neither case loses the old names.
    for(unsigned failure=0;failure<4;++failure){
        ++scenarios;reset();disk=original;BlePeerStore::load();failOpen=failure==0;failSet=failure==1;failCommit=failure==2;commitThenFail=failure==3;
        BlePeerStore::flush();CHECK(sys::revision!=sys::flushed);for(unsigned i=0;i<8;++i)checkPeer(i);
        CHECK(disk==(failure==3?migrated:original));failOpen=failSet=failCommit=commitThenFail=false;BlePeerStore::flush();
        CHECK(sys::revision==sys::flushed&&disk==migrated);reset();BlePeerStore::load();for(unsigned i=0;i<8;++i)checkPeer(i);
    }
    ++scenarios;reset();disk=migrated;BlePeerStore::load();BlePeerStore::remember(peers[8]);commitHook=[]{BlePeerStore::remember(peers[9]);};
    BlePeerStore::flush();CHECK(sys::revision!=sys::flushed);BlePeerStore::flush();CHECK(sys::revision==sys::flushed);reset();BlePeerStore::load();for(unsigned i=0;i<16;++i)checkPeer(i,i<10);
    // Read errors and malformed blobs perform no writes. Exact-size checks
    // happen before copying bytes. No NimBLE or NVS erase API is in this replay.
    for(unsigned failure=0;failure<3;++failure){++scenarios;reset();disk=all;failOpen=failure==0;failQuery=failure==1;failRead=failure==2;BlePeerStore::load();checkEmpty();BlePeerStore::flush();CHECK(!writes&&disk==all);}
    for(size_t length=0;length<all.size()+2;++length){
        if(length==all.size())continue;++scenarios;reset();disk=all;disk.resize(length);const auto damaged=disk;
        BlePeerStore::load();checkEmpty();BlePeerStore::flush();CHECK(!writes&&disk==damaged);
    }
    for(const auto &good:{original,all})for(uint32_t badVersion:{0u,3u,UINT32_MAX}){
        ++scenarios;reset();disk=good;std::memcpy(disk.data(),&badVersion,4);BlePeerStore::load();checkEmpty();BlePeerStore::flush();CHECK(!writes);
    }
    for(const auto &good:{original,all})for(unsigned index:{0u,7u})for(unsigned offset:{0u,7u})for(uint8_t invalid:{uint8_t(4),uint8_t(255)}){
        ++scenarios;reset();disk=good;disk[4+index*72+offset]=invalid;BlePeerStore::load();checkEmpty();BlePeerStore::flush();CHECK(!writes);
    }
    for(const auto &good:{original,all}){
        for(unsigned offset:{0u,7u}){++scenarios;reset();disk=good;disk[disk.size()-72+offset]=255;BlePeerStore::load();checkEmpty();BlePeerStore::flush();CHECK(!writes);}
        ++scenarios;reset();disk=good;std::memcpy(disk.data()+4+72,disk.data()+4,72);BlePeerStore::load();checkEmpty();CHECK(!writes);
        ++scenarios;reset();disk=good;std::memset(disk.data()+4+8,'X',64);BlePeerStore::load();auto bare=peers[0];bare.name[0]=0;auto restored=BlePeerStore::enrich(bare);CHECK(std::strlen(restored.name)==63);CHECK(restored.name[0]=='X');
    }
    CHECK(opens==closes);
    std::printf("%u scenarios; %u checks; legacy 8-to-16 migration, 16 persisted identities, mode labels, target, corruption, no eviction and NVS retries passed\n",scenarios,checks);
}
