#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
namespace sys::gamepad {
// One atomic snapshot: HID button usages 1..24 and D-pad bits in NES order.
// No keyboard events are generated outside the foreground game.
struct State {
    uint32_t buttons=0;
    uint8_t directions=0;
    constexpr uint32_t packed()const{return (buttons&0xffffffu)|(uint32_t(directions&0xf0)<<24);}
    static constexpr State unpack(uint32_t v){return {v&0xffffffu,uint8_t(v>>24)};}
};
inline std::atomic<uint32_t> current{0};
inline std::atomic<uint32_t> rising{0};
inline void publish(State s){const auto v=s.packed();const auto old=current.exchange(v);rising.fetch_or(v&~old);}
inline void reset(){current=0;rising=0;}
inline State snapshot(){return State::unpack(current.load(std::memory_order_acquire));}
inline State takePresses(){return State::unpack(rising.exchange(0));}
constexpr uint8_t fcPad(State s,bool swapAB=false) {
    uint8_t pad=s.directions&0xf0;
    if(s.buttons&1u)pad|=1; // HID button 1 -> A, button 2 -> B
    if(s.buttons&2u)pad|=2;
    if(swapAB)pad=uint8_t((pad&~3u)|((pad&1u)<<1)|((pad&2u)>>1));
    if(s.buttons&(1u<<8))pad|=4;
    if(s.buttons&(1u<<9))pad|=8;
    if((pad&0x30)==0x30)pad&=uint8_t(~0x30);
    if((pad&0xc0)==0xc0)pad&=uint8_t(~0xc0);
    return pad;
}
// Ignore held controls until released after entry, pause, sleep or reconnect.
struct Gate {
    uint32_t blocked=0;
    void clear(State s){blocked=s.packed();}
    State filter(State s){const auto raw=s.packed();blocked&=raw;return State::unpack(raw&~blocked);}
};
inline bool iineKeyboard(const char *name) {
    if(!name)return false;
    char lower[64]{};size_t n=0;
    for(;name[n]&&n<sizeof(lower)-1;++n)lower[n]=name[n]>='A'&&name[n]<='Z'?name[n]+('a'-'A'):name[n];
    return !std::strcmp(lower,"iine-keyboard")||!std::strcmp(lower,"iine-1001");
}
// L1162 H-mode capture: A sends Enter, B sends Space. Keep the documented
// literal A/B assignments too. Consume these usages before a ROM's key map.
struct KeyboardBinding {uint8_t usage,pad;};
inline constexpr KeyboardBinding iineBindings[]={
    {0x04,1},{0x05,2},{0x28,1},{0x2c,2},
    {0x52,0x10},{0x51,0x20},{0x50,0x40},{0x4f,0x80}
};
}
