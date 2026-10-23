#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// YK v1 is little-endian, independent of C++ struct padding. Matching encoder:
// tools/build_knowledge.py. No pointers, LVGL objects or file handles in records.
namespace core::knowledge {
constexpr unsigned HeaderBytes=64, EntryBytes=48, SectionBytes=24;
constexpr unsigned MaxEntries=1024, MaxIndex=128*1024, MaxData=8*1024*1024;
constexpr unsigned MaxBlock=8192, MaxSections=32, MaxToc=4096, MaxRelated=8, MaxSourceLines=128;
constexpr unsigned StateStride=8192, MaxFavorites=512, MaxRecent=30;
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline uint64_t get64(const uint8_t *p){return get32(p)|uint64_t(get32(p+4))<<32;}
inline unsigned get16(const uint8_t *p){return p[0]|unsigned(p[1])<<8;}
inline void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
inline void put64(uint8_t *p,uint64_t n){put32(p,uint32_t(n));put32(p+4,uint32_t(n>>32));}
inline uint32_t crcStep(uint32_t crc,const uint8_t *p,size_t n) {
    while(n--){crc^=*p++;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}return crc;
}
inline uint32_t crc32(const uint8_t *p,size_t n){return ~crcStep(~0u,p,n);}
inline bool range(uint32_t offset,uint32_t size,uint32_t total){return offset<=total && size<=total-offset;}
inline bool utf8(const char *p,size_t n) {
    for(size_t i=0;i<n;) {
        unsigned c=uint8_t(p[i++]);if(c<128){if(c==0 || (c<32 && c!='\n' && c!='\t'))return false;continue;}
        unsigned count=c>=0xc2 && c<=0xdf?1:c>=0xe0 && c<=0xef?2:c>=0xf0 && c<=0xf4?3:0;
        if(!count || i+count>n)return false;
        unsigned first=uint8_t(p[i]);
        if((c==0xe0 && first<0xa0)||(c==0xed && first>=0xa0)||(c==0xf0 && first<0x90)||(c==0xf4 && first>=0x90))return false;
        while(count--)if((uint8_t(p[i++])&0xc0)!=0x80)return false;
    }return true;
}
inline bool blockText(const std::string &text,bool code) {
    if(!utf8(text.data(),text.size()))return false;
    unsigned lines=1,width=0;
    for(unsigned char c:text) {
        if(c=='\n'){if(++lines>MaxSourceLines)return false;width=0;}
        else if(code && (c<32 || c>126 || ++width>240))return false;
        else if(c=='\t')return false; // Package sources expand tabs on the PC.
    }return true;
}
struct Mark {uint64_t id=0;uint32_t section=0,offset=0;};
struct State {
    std::vector<uint64_t> favorites;
    std::vector<Mark> recent;
    bool wrap=false,large=false;
    bool favorite(uint64_t id)const{return std::find(favorites.begin(),favorites.end(),id)!=favorites.end();}
    bool toggle(uint64_t id){auto p=std::find(favorites.begin(),favorites.end(),id);if(p!=favorites.end())favorites.erase(p);else {if(favorites.size()>=MaxFavorites)return false;favorites.push_back(id);}return true;}
    void visit(Mark mark){recent.erase(std::remove_if(recent.begin(),recent.end(),[mark](const Mark &m){return m.id==mark.id;}),recent.end());recent.insert(recent.begin(),mark);if(recent.size()>MaxRecent)recent.resize(MaxRecent);}
};
using StateRecord=std::array<uint8_t,StateStride>;
inline bool validState(const StateRecord &r) {
    if(std::memcmp(r.data(),"YKS1",4)||get32(r.data()+16)>MaxFavorites||get32(r.data()+20)>MaxRecent||
       get32(r.data()+24)>3||get32(r.data()+StateStride-4)!=crc32(r.data(),StateStride-4))return false;
    for(unsigned i=0;i<get32(r.data()+16);++i) {
        auto id=get64(r.data()+32+i*8);if(!id)return false;
        for(unsigned k=0;k<i;++k)if(get64(r.data()+32+k*8)==id)return false;
    }
    for(unsigned i=0;i<get32(r.data()+20);++i) {
        auto p=r.data()+32+MaxFavorites*8+i*16;
        if(!get64(p)||!get32(p+8)||get32(p+12)>MaxBlock)return false;
        for(unsigned k=0;k<i;++k)if(get64(r.data()+32+MaxFavorites*8+k*16)==get64(p))return false;
    }return true;
}
inline int latestState(const StateRecord &a,const StateRecord &b) {
    bool av=validState(a),bv=validState(b);if(!av)return bv?1:-1;if(!bv)return 0;
    return int32_t(get32(b.data()+4)-get32(a.data()+4))>0?1:0;
}
inline void encodeStateInto(StateRecord &r,const State &s,uint32_t sequence) {
    r.fill(0);std::memcpy(r.data(),"YKS1",4);put32(r.data()+4,sequence);
    const auto nf=std::min<size_t>(s.favorites.size(),MaxFavorites),nr=std::min<size_t>(s.recent.size(),MaxRecent);
    put32(r.data()+16,nf);put32(r.data()+20,nr);put32(r.data()+24,(s.wrap?1:0)|(s.large?2:0));
    // Reserved preference revision. Old readers ignore it; bookmarks and the
    // dual-slot/CRC layout stay unchanged. Explicit new wrap choices persist.
    put32(r.data()+28,1);
    for(size_t i=0;i<nf;++i)put64(r.data()+32+i*8,s.favorites[i]);
    for(size_t i=0;i<nr;++i){auto p=r.data()+32+MaxFavorites*8+i*16;put64(p,s.recent[i].id);put32(p+8,s.recent[i].section);put32(p+12,s.recent[i].offset);}
    put32(r.data()+StateStride-4,crc32(r.data(),StateStride-4));
}
inline State decodeState(const StateRecord &r) {
    State s;if(!validState(r))return s;
    // Switch old automatic-wrap defaults once, without dropping reading data.
    s.wrap=get32(r.data()+28)==1 && (get32(r.data()+24)&1)!=0;
    s.large=(get32(r.data()+24)&2)!=0;
    for(unsigned i=0;i<get32(r.data()+16);++i)s.favorites.push_back(get64(r.data()+32+i*8));
    for(unsigned i=0;i<get32(r.data()+20);++i){auto p=r.data()+32+MaxFavorites*8+i*16;s.recent.push_back({get64(p),get32(p+8),get32(p+12)});}return s;
}
}
