// Synthetic addresses only. Compile-time tests execute the production resolver
// wrapper; the SDK's AES/IRK resolution and real iPhone interoperability remain
// hardware/integration checks, not simulated cryptographic claims.
#include "core/ble_peer_identity.hpp"
struct Address {uint8_t type=0,val[6]{};};
constexpr bool equal(Address a,Address b) {
    if(a.type!=b.type)return false;
    for(unsigned i=0;i<6;++i)if(a.val[i]!=b.val[i])return false;
    return true;
}
constexpr Address publicPeer{0,{1,2,3,4,5,6}};
constexpr Address randomPeer{1,{7,8,9,10,11,0xc1}};
constexpr bool syntheticResolver(uint8_t *rpa,uint8_t *id,uint8_t *type) {
    const auto peer=rpa[0]==42?publicPeer:randomPeer;
    if(rpa[0]!=42 && rpa[0]!=43)return false;
    *type=peer.type;for(unsigned i=0;i<6;++i)id[i]=peer.val[i];return true;
}
constexpr bool allAddressKinds() {
    Address result{};
    for(unsigned type=0;type<4;++type) {
        auto input=type&1?randomPeer:publicPeer;input.type=type;
        if(!sys::ble::resolveIdentity(input,result,syntheticResolver))return false;
        if(!equal(result,type&1?randomPeer:publicPeer))return false;
    }
    return true;
}
constexpr bool rotatedRpas() {
    Address result{};
    for(unsigned peer=42;peer<=43;++peer)for(unsigned random=0;random<256;++random) {
        Address input{1,{uint8_t(peer),uint8_t(random),44,55,66,0x40}};
        if(!sys::ble::resolveIdentity(input,result,syntheticResolver))return false;
        if(!equal(result,peer==42?publicPeer:randomPeer))return false;
        if(equal(result,peer==42?randomPeer:publicPeer))return false;
    }
    return true;
}
constexpr bool rejectsUnknownOrBrokenResolver() {
    Address out{},unknown{1,{99,2,3,4,5,0x40}},rpa{1,{42,2,3,4,5,0x40}};
    if(sys::ble::resolveIdentity(unknown,out,syntheticResolver))return false;
    if(sys::ble::resolveIdentity(Address{},out,syntheticResolver))return false;
    for(unsigned type=4;type<256;++type) {
        auto bad=publicPeer;bad.type=type;
        if(sys::ble::resolveIdentity(bad,out,syntheticResolver))return false;
    }
    if(sys::ble::resolveIdentity(rpa,out,[](uint8_t *,uint8_t *,uint8_t *){return true;}))return false;
    if(sys::ble::resolveIdentity(rpa,out,[](uint8_t *,uint8_t *id,uint8_t *type){id[0]=1;*type=255;return true;}))return false;
    return true;
}
static_assert(allAddressKinds());
static_assert(rotatedRpas());
static_assert(rejectsUnknownOrBrokenResolver());
static_assert(sys::ble::ManualAdvertisingInterval*625==20000);
