#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace sys::hidinput {
struct Keys {
    std::array<uint8_t,32> bits{};
    constexpr bool down(unsigned key) const { return key<256 && (bits[key/8]&(1u<<(key%8))); }
    constexpr void set(unsigned key) { if(key<256) bits[key/8]|=1u<<(key%8); }
};
// Bounded HID short-item parser: keyboard arrays (6KRO) and variable bitmaps
// (NKRO), arbitrary report IDs/bit offsets. Unknown/vendor collections ignored.
class ReportMap {
    struct Field { uint16_t offset=0,count=0; uint8_t size=0,id=0; bool variable=false; uint16_t first=0,last=0; std::array<uint16_t,32> usages{}; uint8_t usageCount=0; };
    std::array<Field,32> fields{};
    std::array<uint8_t,16> ids{};
    std::array<uint16_t,16> lengths{};
    size_t count=0,idCount=0;
public:
    constexpr bool parse(const uint8_t *data,size_t length) {
        *this={};
        struct Global { uint32_t page=0,size=0,count=0; uint8_t id=0; } g,stack[4]{};
        unsigned depth=0; uint16_t first=0,last=0; std::array<uint16_t,32> usages{}; unsigned usageCount=0;
        if(!data || !length || length>2048) return false;
        for(size_t pos=0;pos<length;) {
            const uint8_t tag=data[pos++]; if(tag==0xfe) return false;
            const unsigned n=(tag&3)==3?4:tag&3;
            if(pos+n>length) return false;
            uint32_t value=0; for(unsigned j=0;j<n;++j) value|=uint32_t(data[pos++])<<(j*8);
            const unsigned type=(tag>>2)&3, item=tag>>4;
            if(type==1) {
                switch(item) {
                case 0:g.page=value;break;
                case 7:g.size=value;break;
                case 8:if(!value || value>255) return false;g.id=value;break;
                case 9:g.count=value;break;
                case 10:if(depth==4) return false;stack[depth++]=g;break;
                case 11:if(!depth) return false;g=stack[--depth];break;
                default:break;
                }
            } else if(type==2) {
                if(item==0) { if(usageCount>=usages.size()) return false; usages[usageCount++]=value&0xffff; }
                else if(item==1) first=value&0xffff;
                else if(item==2) last=value&0xffff;
            } else if(type==0) {
                if(item==8) {
                    if(g.size>32 || g.count>512 || g.size*g.count>512) return false;
                    size_t slot=0; for(;slot<idCount && ids[slot]!=g.id;++slot) {}
                    if(slot==idCount) { if(idCount==ids.size()) return false;ids[idCount++]=g.id; }
                    if(lengths[slot]+g.size*g.count>512) return false;
                    if(g.page==7 && !(value&1) && g.size && g.count) {
                        if(count==fields.size()) return false;
                        auto &f=fields[count++];f.offset=lengths[slot];f.count=g.count;f.size=g.size;f.id=g.id;
                        f.variable=value&2;f.first=first;f.last=last;f.usages=usages;f.usageCount=usageCount;
                        if(f.variable && !usageCount && (last<first || static_cast<uint32_t>(last-first+1)<g.count)) return false;
                    }
                    lengths[slot]+=g.size*g.count;
                }
                first=last=0;usageCount=0;usages={};
            }
        }
        return count!=0;
    }
    constexpr bool has(uint8_t id) const { for(size_t i=0;i<count;++i) if(fields[i].id==id) return true; return false; }
    constexpr bool decode(uint8_t id,const uint8_t *data,size_t length,Keys &out) const {
        if(!data || !has(id)) return false;
        for(size_t i=0;i<idCount;++i) if(ids[i]==id && length*8<lengths[i]) return false;
        Keys decoded;
        for(size_t i=0;i<count;++i) {
            const auto &f=fields[i];if(f.id!=id) continue;
            for(unsigned j=0;j<f.count;++j) {
                uint32_t value=0;const unsigned offset=f.offset+j*f.size;
                for(unsigned bit=0;bit<f.size;++bit) value|=uint32_t((data[(offset+bit)/8]>>((offset+bit)%8))&1)<<bit;
                const unsigned usage=f.variable?(j<f.usageCount?f.usages[j]:f.first+j):value;
                if(!f.variable && usage>=1 && usage<=3) return false; // rollover/error: preserve last report
                if(value && usage>=4 && usage<256) decoded.set(usage);
            }
        }
        out=decoded;return true;
    }
};
} // namespace sys::hidinput
