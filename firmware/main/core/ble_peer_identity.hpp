#pragma once
#include <cstdint>

namespace sys::ble {
// Bluetooth address types: public/random and their resolved identity forms.
// This helper accepts an SDK address-shaped value; the supplied resolver must
// authenticate an RPA against the saved peer IRKs, never a name or chosen row.
template<class Address,class Resolver>
constexpr bool resolveIdentity(Address input,Address &identity,Resolver resolver) {
    identity={};
    if(input.type>3)return false;
    bool nonzero=false;for(auto byte:input.val)nonzero|=byte!=0;
    if(!nonzero)return false;
    if(input.type==1 && (input.val[5]&0xc0)==0x40) {
        uint8_t resolved[6]{},type=0;
        if(!resolver(input.val,resolved,&type) || type>3)return false;
        nonzero=false;for(auto byte:resolved)nonzero|=byte!=0;
        if(!nonzero)return false;
        identity.type=type&1;
        for(unsigned i=0;i<6;++i)identity.val[i]=resolved[i];
    } else {
        identity=input;identity.type&=1;
    }
    return true;
}
// Fixed 20 ms throughout the user-requested 60-second invitation. Avoid a
// mid-window stop/start race and the old 100-150 ms non-discoverable range.
// Apple QA1931 recommends an exact 20 ms interval for at least 30 seconds.
constexpr uint16_t ManualAdvertisingInterval=32; // 0.625 ms units
} // namespace sys::ble
