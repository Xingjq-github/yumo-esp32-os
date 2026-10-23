#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sys::app_order {
constexpr size_t Capacity=32;
// Keys come from registration, not translated labels or firmware array positions.
constexpr uint32_t key(const char *text){
    uint32_t value=2166136261u;
    for(;*text;++text)value=(value^uint8_t(*text))*16777619u;
    return value?value:1;
}
constexpr uint32_t Settings=key("settings");
struct Order {
    std::array<uint32_t,Capacity> ids{};
    size_t count=0;
    constexpr size_t find(uint32_t id) const {for(size_t i=0;i<count;++i)if(ids[i]==id)return i;return count;}
    constexpr bool operator==(const Order &other)const {
        if(count!=other.count)return false;
        for(size_t i=0;i<count;++i)if(ids[i]!=other.ids[i])return false;
        return true;
    }
};
constexpr bool valid(const Order &order){
    if(order.count>Capacity)return false;
    for(size_t i=0;i<order.count;++i){
        if(!order.ids[i])return false;
        for(size_t j=0;j<i;++j)if(order.ids[i]==order.ids[j])return false;
    }
    return true;
}
constexpr Order normalize(const Order &saved,const Order &registered){
    Order result;
    auto append=[&](uint32_t id){
        if(id&&id!=Settings&&registered.find(id)<registered.count&&result.find(id)==result.count&&result.count<Capacity)
            result.ids[result.count++]=id;
    };
    if(valid(saved))for(size_t i=0;i<saved.count;++i)append(saved.ids[i]);
    for(size_t i=0;i<registered.count&&i<Capacity;++i)append(registered.ids[i]);
    if(registered.find(Settings)<registered.count&&result.count<Capacity)result.ids[result.count++]=Settings;
    return result;
}
constexpr bool move(Order &order,size_t from,size_t to){
    if(from>=order.count||to>=order.count||from==to||order.ids[from]==Settings||order.ids[to]==Settings)return false;
    const auto id=order.ids[from];
    if(from<to)for(size_t i=from;i<to;++i)order.ids[i]=order.ids[i+1];
    else for(size_t i=from;i>to;--i)order.ids[i]=order.ids[i-1];
    order.ids[to]=id;return true;
}
// Fixed-size, versioned little-endian blob. Reject corrupt/unknown payloads.
using Encoded=std::array<uint8_t,12+Capacity*4>;
constexpr void put(Encoded &out,size_t at,uint32_t value){for(unsigned i=0;i<4;++i)out[at+i]=uint8_t(value>>(i*8));}
constexpr uint32_t get(const Encoded &in,size_t at){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(in[at+i])<<(i*8);return value;}
constexpr uint32_t checksum(const Encoded &in){uint32_t value=2166136261u;for(size_t i=0;i<in.size()-4;++i)value=(value^in[i])*16777619u;return value;}
constexpr Encoded encode(const Order &order){
    Encoded out{};put(out,0,0x314f4d59);put(out,4,uint32_t(order.count));
    for(size_t i=0;i<order.count&&i<Capacity;++i)put(out,8+4*i,order.ids[i]);
    put(out,out.size()-4,checksum(out));return out;
}
constexpr Order decode(const Encoded &in){
    Order out;
    if(get(in,0)!=0x314f4d59||get(in,4)>Capacity||get(in,in.size()-4)!=checksum(in))return out;
    out.count=get(in,4);for(size_t i=0;i<out.count;++i)out.ids[i]=get(in,8+4*i);
    return valid(out)?out:Order{};
}
enum class SaveState { Saved, Pending, Failed };
} // namespace sys::app_order
