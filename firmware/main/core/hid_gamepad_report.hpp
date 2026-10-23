#pragma once
#include "gamepad_input.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace sys::hidinput {
// HOGP Report Reference supplies the ID separately from each notification.
// Parse actual offsets/ranges instead of assuming a named controller's packet.
class GamepadReportMap {
    enum class Kind:uint8_t {Button,X,Y,Hat,Up,Down,Left,Right};
    struct Field {uint16_t offset=0;uint8_t size=0,id=0,button=0;Kind kind=Kind::Button;int64_t low=0,high=0;};
    std::array<Field,96> fields{};
    std::array<uint8_t,16> ids{};
    std::array<uint16_t,16> bits{};
    size_t count=0,idCount=0;
    static constexpr int64_t signedValue(uint32_t v,unsigned bytes) {
        return bytes && (v&(uint32_t(1)<<(bytes*8-1)))?int64_t(v)-(int64_t(1)<<(bytes*8)):v;
    }
public:
    bool parse(const uint8_t *data,size_t length) {
        count=idCount=0;bits.fill(0);
        if(!data||!length||length>2048)return false;
        struct Global {uint32_t page=0,size=0,count=0;uint8_t id=0;int64_t low=0,high=0;} g,stack[4]{};
        unsigned depth=0,collection=0;bool game=false,collections[16]{};
        std::array<uint32_t,64> usages{};unsigned used=0;uint32_t first=0,last=0;bool range=false;
        for(size_t pos=0;pos<length;) {
            const uint8_t prefix=data[pos++];if(prefix==0xfe)return false;
            const unsigned n=(prefix&3)==3?4:prefix&3;if(pos+n>length)return false;
            uint32_t v=0;for(unsigned j=0;j<n;++j)v|=uint32_t(data[pos++])<<(8*j);
            const unsigned type=(prefix>>2)&3,item=prefix>>4;
            if(type==1) {
                switch(item) {
                case 0:g.page=v;break;
                case 1:g.low=signedValue(v,n);break;
                case 2:g.high=g.low<0?signedValue(v,n):int64_t(v);break;
                case 7:g.size=v;break;
                case 8:if(!v||v>255)return false;g.id=v;break;
                case 9:g.count=v;break;
                case 10:if(depth==4)return false;stack[depth++]=g;break;
                case 11:if(!depth)return false;g=stack[--depth];break;
                default:break;
                }
            } else if(type==2) {
                const uint32_t u=n==4?v:(g.page<<16)|(v&0xffff);
                if(item==0){if(used==usages.size())return false;usages[used++]=u;}
                else if(item==1){first=u;range=true;}
                else if(item==2)last=u;
            } else if(type==0) {
                if(item==10) {
                    if(collection==16)return false;
                    collections[collection++]=game;
                    // Only a joystick/gamepad application, never a mouse axis.
                    if(v==1)game=used&&(usages[0]==0x10004||usages[0]==0x10005||usages[0]==0x10008);
                } else if(item==12) {
                    if(!collection)return false;
                    game=collections[--collection];
                } else if(item==8) {
                    if(g.size>32||g.count>512||g.size*g.count>512)return false;
                    size_t slot=0;for(;slot<idCount&&ids[slot]!=g.id;++slot){}
                    if(slot==idCount){if(idCount==ids.size())return false;ids[idCount++]=g.id;}
                    if(bits[slot]+g.size*g.count>512)return false;
                    if(game&&!(v&1)&&(v&2)&&!(v&4)&&g.size&&g.count) {
                        for(unsigned j=0;j<g.count;++j) {
                            const auto u=j<used?usages[j]:range&&last>=first&&j<=last-first?first+j:0;
                            Kind kind;uint8_t button=0;
                            if((u>>16)==9&&(u&0xffff)>=1&&(u&0xffff)<=24){kind=Kind::Button;button=(u&0xffff)-1;}
                            else if(u==0x10030)kind=Kind::X;
                            else if(u==0x10031)kind=Kind::Y;
                            else if(u==0x10039)kind=Kind::Hat;
                            else if(u>=0x10090&&u<=0x10093)kind=u==0x10090?Kind::Up:u==0x10091?Kind::Down:u==0x10092?Kind::Right:Kind::Left;
                            else continue;
                            if(g.high<g.low)return false;
                            if((kind==Kind::X||kind==Kind::Y)&&g.high==g.low)continue;
                            if(kind==Kind::Hat&&g.high-g.low!=7)continue;
                            if(count==fields.size())return false;
                            fields[count++]={uint16_t(bits[slot]+j*g.size),uint8_t(g.size),g.id,button,kind,g.low,g.high};
                        }
                    }
                    bits[slot]+=g.size*g.count;
                }
                used=0;first=last=0;range=false;
            }
        }
        return count&&!collection&&!depth;
    }
    bool has(uint8_t id)const{for(size_t i=0;i<count;++i)if(fields[i].id==id)return true;return false;}
    bool decode(uint8_t id,const uint8_t *data,size_t length,gamepad::State &out)const {
        if(!data||!has(id)||length>64)return false;
        for(size_t i=0;i<idCount;++i)if(ids[i]==id&&length*8<bits[i])return false;
        gamepad::State s;
        for(size_t i=0;i<count;++i) {
            const auto &f=fields[i];if(f.id!=id)continue;
            uint32_t raw=0;for(unsigned b=0;b<f.size;++b)raw|=uint32_t((data[(f.offset+b)/8]>>((f.offset+b)%8))&1)<<b;
            const int64_t v=f.low<0&&(raw&(uint32_t(1)<<(f.size-1)))?int64_t(raw)-(int64_t(1)<<f.size):int64_t(raw);
            if(v<f.low||v>f.high)continue; // null hat / out-of-range axis is neutral
            if(f.kind==Kind::Button){if(v)s.buttons|=1u<<f.button;}
            else if(f.kind==Kind::Hat) {
                constexpr uint8_t hat[]={0x10,0x90,0x80,0xa0,0x20,0x60,0x40,0x50};
                s.directions|=hat[v-f.low];
            } else if(f.kind==Kind::X||f.kind==Kind::Y) {
                // Central half is a dead zone, so idle analog noise never moves.
                const int64_t d=(v-f.low)*4,span=f.high-f.low;
                if(d<span)s.directions|=f.kind==Kind::X?0x40:0x10;
                if(d>span*3)s.directions|=f.kind==Kind::X?0x80:0x20;
            } else if(v) s.directions|=f.kind==Kind::Up?0x10:f.kind==Kind::Down?0x20:f.kind==Kind::Left?0x40:0x80;
        }
        out=s;return true;
    }
};
}
