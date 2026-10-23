#pragma once
#include <cstdint>
#include <initializer_list>
namespace sys::lan {
// Host-order IPv4 arithmetic, independent of ESP-IDF for boundary tests.
constexpr uint32_t Mask=0xffffff00u;
constexpr bool overlaps(uint32_t ap,uint32_t upstream,uint32_t mask) {
    const auto common=Mask&mask;return (ap&common)==(upstream&common);
}
constexpr uint32_t chooseAddress(uint32_t upstream,uint32_t mask,uint32_t current=0xc0a80401u) {
    if(!upstream || !mask || !overlaps(current,upstream,mask))return current;
    for(auto candidate:{0xc0a80401u,0xc0a84d01u,0xac1f4d01u,0x0a4d0001u})
        if(!overlaps(candidate,upstream,mask))return candidate;
    return 0; // Refuse ambiguous routing rather than reuse an overlapping subnet.
}
}
