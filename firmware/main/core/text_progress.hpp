#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace core::reading {
struct Mark {
    uint64_t fingerprint=0;
    uint32_t page=1, offset=0;
};
// Two alternating checked records retain the previous position after a torn write.
using Record=std::array<uint8_t,32>;
constexpr long RecordStride=512; // keep slots in separate logical SD sectors
constexpr uint64_t fingerprint(const char *data,size_t size) {
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;i<size;++i) hash=(hash^static_cast<uint8_t>(data[i]))*1099511628211ULL;
    return hash;
}
constexpr void put(Record &r,size_t at,uint64_t value,size_t bytes) {
    for(size_t i=0;i<bytes;++i) r[at+i]=static_cast<uint8_t>(value>>(8*i));
}
constexpr uint64_t get(const Record &r,size_t at,size_t bytes) {
    uint64_t value=0;
    for(size_t i=0;i<bytes;++i) value|=uint64_t(r[at+i])<<(8*i);
    return value;
}
constexpr uint32_t checksum(const Record &r) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<28;++i) hash=(hash^r[i])*16777619u;
    return hash;
}
constexpr Record encode(Mark mark,uint32_t sequence) {
    Record r{};
    put(r,0,0x31524459,4); // YDR1
    put(r,4,sequence,4); put(r,8,mark.fingerprint,8);
    put(r,16,mark.page,4); put(r,20,mark.offset,4);
    put(r,28,checksum(r),4);
    return r;
}
constexpr bool valid(const Record &r) {
    return get(r,0,4)==0x31524459 && get(r,16,4)>0 && get(r,28,4)==checksum(r);
}
constexpr bool newer(uint32_t a,uint32_t b) { return a!=b && uint32_t(a-b)<0x80000000u; }
constexpr int latest(const Record &a,const Record &b) {
    return !valid(a)?(valid(b)?1:-1):!valid(b)?0:
        newer(static_cast<uint32_t>(get(b,4,4)),static_cast<uint32_t>(get(a,4,4)))?1:0;
}
constexpr Mark decode(const Record &r) {
    return {get(r,8,8),static_cast<uint32_t>(get(r,16,4)),static_cast<uint32_t>(get(r,20,4))};
}
} // namespace core::reading
