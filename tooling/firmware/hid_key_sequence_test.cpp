// Compile the real production queue/sequencer and exact HID bytes with the
// ESP32-S3 compiler. Static assertions are not a macOS/BLE hardware test.
#include "apps/hid_keyboard_model.hpp"
#include "core/hid_key_sequence.hpp"
using sys::hid::KeySequence;
using sys::hid::KeyQueue;
using sys::hid::QueuedKey;
using Phase=KeySequence::Phase;
constexpr bool zero(std::array<uint8_t,8> r) { for(auto b:r)if(b)return false;return true; }
constexpr bool exactMacBytes() {
    constexpr uint8_t code[]={0x06,0x1b,0x19,0x04,0x14,0x28,0x29,0x52,0x1d,0x50,0x51,0x4f};
    constexpr uint8_t mods[]={0x08,0x08,0x08,0x08,0x09,0,0,0,0x08,0,0,0};
    for(unsigned i=0;i<12;++i) {
        const auto &s=apps::keys::shortcuts[i];
        if(s.mac.code!=code[i] || s.mac.modifiers!=mods[i])return false;
    }
    return apps::keys::Ctrl==0x01 && apps::keys::Shift==0x02 &&
           apps::keys::Alt==0x04 && apps::keys::Gui==0x08;
}
constexpr bool stroke(uint8_t key,uint8_t mods) {
    KeySequence s;s.start(key,mods);
    if(mods) {
        if(s.phase!=Phase::Modifiers || s.report()!=std::array<uint8_t,8>{mods,0,0,0,0,0,0,0})return false;
        if(s.delayMs()!=20)return false;
        s.sent(true);
    }
    if(s.phase!=Phase::Press || s.report()!=std::array<uint8_t,8>{mods,0,key,0,0,0,0,0})return false;
    if(s.delayMs()!=40)return false;
    s.sent(true);
    if(mods) {
        if(s.phase!=Phase::KeyUp || s.report()!=std::array<uint8_t,8>{mods,0,0,0,0,0,0,0})return false;
        if(s.delayMs()!=20)return false;
        s.sent(true);
    }
    if(s.phase!=Phase::Release || !zero(s.report()) || s.delayMs()!=20)return false;
    s.sent(true);s.cancel();return s.phase==Phase::Done;
}
constexpr bool allShortcutsAndAscii() {
    for(auto s:apps::keys::shortcuts)if(!stroke(s.win.code,s.win.modifiers)||!stroke(s.mac.code,s.mac.modifiers))return false;
    for(int c=32;c<=126;++c) {auto s=apps::keys::ascii(c);if(!stroke(s.code,s.modifiers))return false;}
    return stroke(0x28,0)&&stroke(0x2b,0)&&stroke(0x2a,0);
}
constexpr bool cancellation() {
    for(unsigned mods:{0u,8u,9u})for(unsigned step=0;step<(mods?4u:2u);++step) {
        KeySequence s;s.start(0x06,mods);
        for(unsigned i=0;i<step;++i)s.sent(true);
        s.cancel();s.cancel(); // no new key-down, modifiers eventually cleared
        if(s.phase!=Phase::Release || !zero(s.report()))return false;
        s.sent(true);s.cancel();if(s.phase!=Phase::Done)return false;
    }
    return true;
}
constexpr bool failuresOnlyRetryRelease() {
    for(unsigned step=0;step<4;++step) {
        KeySequence s;s.start(0x06,8);
        for(unsigned i=0;i<step;++i)s.sent(true);
        s.sent(false);s.cancel();
        if(s.phase!=Phase::Release || !zero(s.report()))return false;
        s.sent(false);if(s.phase!=Phase::Release || !zero(s.report()))return false;
        s.sent(false);if(s.phase!=Phase::Abort)return false;
        s.cancel();if(s.phase!=Phase::Abort)return false;
        s.start(0x19,8);s.sent(false);s.sent(true);
        if(s.phase!=Phase::Done)return false;
    }
    return true;
}
constexpr bool boundedFifo() {
    KeyQueue q;QueuedKey got{};
    if(q.pop(got))return false;
    for(unsigned i=0;i<8;++i)if(!q.push({static_cast<uint8_t>(i+4),8,100+i,200+i}))return false;
    if(q.push({0x19,8,999,999}) || q.count!=8)return false;
    for(unsigned i=0;i<4;++i) {
        if(!q.pop(got) || got.key!=i+4 || got.link!=100+i || got.epoch!=200+i)return false;
        if(q.slots[i].key || q.slots[i].modifiers || q.slots[i].link || q.slots[i].epoch)return false;
    }
    for(unsigned i=0;i<4;++i)if(!q.push({static_cast<uint8_t>(i+12),0,300+i,400+i}))return false;
    for(unsigned i=4;i<12;++i)if(!q.pop(got) || got.key!=i+4)return false;
    if(q.count || q.pop(got))return false;
    q.push({0x06,8,1,1});q.clear();
    for(auto key:q.slots)if(key.key || key.modifiers || key.link || key.epoch)return false;
    return !q.count && !q.head && q.push({0x19,8,2,2}) && q.pop(got) && got.link==2;
}
static_assert(exactMacBytes());
static_assert(allShortcutsAndAscii());
static_assert(cancellation());
static_assert(failuresOnlyRetryRelease());
static_assert(boundedFifo());
