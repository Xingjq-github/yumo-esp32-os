// Addresses and names below are synthetic. No real device/NVS is accessed.
#include "core/ble_peer_store.hpp"
#include "core/system_service.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
static unsigned checks=0,commits=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define ESP_LOGW(...)
using nvs_handle_t=int;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
static std::vector<uint8_t> disk;
static int nvs_open(const char *name,int,nvs_handle_t *h){CHECK(!std::strcmp(name,"ble_peers"));*h=1;return 0;}
static void nvs_close(nvs_handle_t){}
static int nvs_get_blob(nvs_handle_t,const char *key,void *value,size_t *length){CHECK(!std::strcmp(key,"v1"));if(disk.empty()||*length<disk.size())return 1;*length=disk.size();std::memcpy(value,disk.data(),disk.size());return 0;}
static int nvs_set_blob(nvs_handle_t,const char *key,const void *value,size_t length){CHECK(!std::strcmp(key,"v1"));auto p=static_cast<const uint8_t *>(value);disk.assign(p,p+length);return 0;}
static int nvs_commit(nvs_handle_t){++commits;return 0;}
#include "peer_store.inc"
int main(){
    using sys::BlePeerStore;
    static_assert(std::tuple_size<decltype(sys::BluetoothSnapshot{}.bonds)>::value==8);
    CHECK(sys::saved.records.size()==8);
    std::array<sys::BleBond,8> peers{};
    const char *names[]={"IINE-keyboard","Xbox Wireless Controller","Wireless Controller","IINE-keyboard","Keyboard 5","Keyboard 6","Keyboard 7","Keyboard 8"};
    for(unsigned i=0;i<peers.size();++i){
        auto &p=peers[i];p.addrType=1;p.addr[0]=uint8_t(i+1);p.addr[5]=0xc0;p.keyboard=true;
        std::snprintf(p.name,sizeof(p.name),"%s",names[i]);BlePeerStore::remember(p);
    }
    for(unsigned i=0;i<peers.size();++i)for(unsigned j=0;j<peers.size();++j)CHECK(BlePeerStore::same(peers[i],peers[j])==(i==j));
    BlePeerStore::setTarget(peers[1]);BlePeerStore::flush();CHECK(commits==1);
    const auto original=disk;
    // Same v1 binary layout: all eight records and recent target survive restart.
    sys::saved={};BlePeerStore::load();CHECK(BlePeerStore::same(BlePeerStore::target(),peers[1]));
    for(const auto &p:peers){auto bare=p;bare.name[0]=0;bare.keyboard=false;auto restored=BlePeerStore::enrich(bare);CHECK(restored.keyboard&&!std::strcmp(restored.name,p.name));}
    for(unsigned i=0;i<3;++i){
        BlePeerStore::activate(peers[i]);BlePeerStore::name("HID Keyboard");
        CHECK(!std::strcmp(BlePeerStore::active().name,names[i]));
    }
    BlePeerStore::flush();CHECK(commits==1&&disk==original);
    // A full metadata table never evicts an existing identity implicitly.
    auto ninth=peers[0];ninth.addr[0]=9;BlePeerStore::remember(ninth);BlePeerStore::flush();CHECK(disk==original);
    // Resolved-address aliases retain one identity, while other modes stay distinct.
    auto resolved=peers[0];resolved.addrType=3;CHECK(BlePeerStore::same(resolved,peers[0]));
    BlePeerStore::forget(peers[1]);BlePeerStore::flush();CHECK(commits==2);
    sys::saved={};BlePeerStore::load();CHECK(!BlePeerStore::target().addr[0]);
    for(unsigned i=0;i<8;++i){auto bare=peers[i];bare.name[0]=0;auto restored=BlePeerStore::enrich(bare);CHECK((restored.name[0]!=0)==(i!=1));}
    // Empty input labels and remote hosts still accept their GAP display name.
    auto unnamed=peers[1];unnamed.name[0]=0;BlePeerStore::activate(unnamed);BlePeerStore::name("New keyboard");CHECK(!std::strcmp(BlePeerStore::active().name,"New keyboard"));
    auto phone=peers[1];phone.keyboard=false;BlePeerStore::activate(phone);BlePeerStore::name("Phone");CHECK(!std::strcmp(BlePeerStore::active().name,"Phone"));
    std::printf("%u checks; eight records, three independent modes, restart, stable mode labels and targeted deletion passed\n",checks);
}
