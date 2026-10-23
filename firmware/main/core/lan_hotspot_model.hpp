#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace sys::lan {
inline constexpr unsigned MaxClients=4;
inline constexpr char DefaultSsid[]="Yumo-LAN";
inline constexpr char DefaultPassword[]="12345678";
inline constexpr int64_t TransitionUs=5000000;
inline constexpr int64_t SampleUs=1000000;
inline constexpr int ClientHeight=80;

enum class ConfigError { None, Name, Password };
inline ConfigError validate(const std::string &ssid,const std::string &password) {
    if(ssid.empty() || ssid.size()>32 || ssid.find('\0')!=std::string::npos ||
       ssid.find_first_of("\r\n\t")!=std::string::npos || ssid.find_first_not_of(' ')==std::string::npos)
        return ConfigError::Name;
    if(password.size()<8 || password.size()>63 ||
       !std::all_of(password.begin(),password.end(),[](unsigned char c){return c>=32 && c<=126;}))
        return ConfigError::Password;
    return ConfigError::None;
}
inline bool sameMac(const uint8_t *a,const uint8_t *b) { return std::memcmp(a,b,6)==0; }
inline uint32_t rate(uint64_t bytes,int64_t elapsedUs) {
    if(elapsedUs<=0) return 0;
    // Keep integer arithmetic bounded even if a corrupt/very old sample is supplied.
    constexpr uint64_t Max=std::numeric_limits<uint32_t>::max();
    if(bytes>std::numeric_limits<uint64_t>::max()/1000000ULL) return Max;
    const auto value=bytes*1000000ULL/static_cast<uint64_t>(elapsedUs);
    return static_cast<uint32_t>(std::min<uint64_t>(Max,value));
}
struct Counter {
    bool used=false;
    uint8_t mac[6]{};
    uint64_t upload=0,download=0;
    std::array<uint16_t,17> sequence{};
    uint32_t sequenceValid=0;
};
// Fixed-size task-context bookkeeping. Caller supplies the short critical section.
// Only headers/lengths are inspected: never store payloads, credentials or packets.
struct Meter {
    std::array<Counter,MaxClients> clients{};
    uint8_t bssid[6]{};
    int find(const uint8_t *mac) const {
        for(unsigned i=0;i<MaxClients;++i) if(clients[i].used && sameMac(clients[i].mac,mac)) return static_cast<int>(i);
        return -1;
    }
    void uplink(const uint8_t *frame,size_t size) {
        if(!frame || size<28) return;
        const uint16_t fc=frame[0]|uint16_t(frame[1])<<8;
        // Data (not null-data), station -> this AP only. Ignore neighbouring BSSes.
        if((fc&0x000f)!=0x0008 || (fc&0x0300)!=0x0100 || (fc&0x0040) || !sameMac(frame+4,bssid)) return;
        const int peer=find(frame+10); if(peer<0) return;
        const bool qos=(fc&0x0080)!=0;
        const size_t header=24+(qos?2:0)+((qos && (fc&0x8000))?4:0);
        if(size<header+4) return;
        const unsigned tid=qos?(frame[24]&15):16;
        const uint16_t seq=frame[22]|uint16_t(frame[23])<<8;
        auto &c=clients[peer];
        if((fc&0x0800) && (c.sequenceValid&(1u<<tid)) && c.sequence[tid]==seq) return;
        c.sequence[tid]=seq; c.sequenceValid|=1u<<tid;
        c.upload+=size;
    }
    void downlink(const uint8_t *frame,size_t size) {
        if(!frame || size<14) return;
        // IDF TxDone normally supplies the transmitted Ethernet buffer. Accept
        // validated FromDS headers too; never guess a peer from an arbitrary offset.
        int peer=find(frame);
        if(peer<0 && size>=24) {
            const uint16_t fc=frame[0]|uint16_t(frame[1])<<8;
            if((fc&0x000f)==0x0008 && (fc&0x0300)==0x0200 && !(fc&0x0040) && sameMac(frame+10,bssid))
                peer=find(frame+4);
        }
        if(peer>=0) clients[peer].download+=size;
    }
};
} // namespace sys::lan