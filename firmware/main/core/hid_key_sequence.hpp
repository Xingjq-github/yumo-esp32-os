#pragma once
#include <array>
#include <cstdint>

namespace sys::hid {
struct QueuedKey { uint8_t key=0,modifiers=0;uintptr_t link=0;uint32_t epoch=0; };
struct KeyQueue {
    static constexpr unsigned Capacity=8;
    std::array<QueuedKey,Capacity> slots{};
    unsigned head=0,count=0;
    constexpr bool push(QueuedKey key) {
        if(count==Capacity)return false;
        slots[(head+count)%Capacity]=key;++count;return true;
    }
    constexpr bool pop(QueuedKey &key) {
        if(!count)return false;
        key=slots[head];slots[head]={};head=(head+1)%Capacity;--count;return true;
    }
    constexpr void clear() { slots={};head=count=0; }
};

// USB HID 1.11 Appendix B: modifiers, reserved byte, six keyboard usages.
// macOS shortcuts use GUI (Command), not Control. Order modifiers before the
// main key, and release the main key before its modifiers (Apple 102650).
// These delays are a compatibility policy, NOT USB/Apple-specified timings.
struct KeySequence {
    enum class Phase : uint8_t { Modifiers, Press, KeyUp, Release, Done, Abort };
    static constexpr uint32_t EdgeMs=20, HoldMs=40;
    static constexpr unsigned MaxFailures=3;
    uint8_t key=0, modifiers=0;
    unsigned failures=0;
    Phase phase=Phase::Done;
    constexpr void start(uint8_t k,uint8_t m) {
        key=k;modifiers=m;failures=0;
        phase=m?Phase::Modifiers:Phase::Press;
    }
    constexpr void cancel() {
        if(phase==Phase::Modifiers || phase==Phase::Press || phase==Phase::KeyUp)
            phase=Phase::Release;
    }
    constexpr std::array<uint8_t,8> report() const {
        return {static_cast<uint8_t>(phase==Phase::Release?0:modifiers),0,
                static_cast<uint8_t>(phase==Phase::Press?key:0),0,0,0,0,0};
    }
    constexpr uint32_t delayMs() const { return phase==Phase::Press?HoldMs:EdgeMs; }
    constexpr void sent(bool ok) {
        if(!ok) { phase=++failures>=MaxFailures?Phase::Abort:Phase::Release;return; }
        switch(phase) {
        case Phase::Modifiers:phase=Phase::Press;break;
        case Phase::Press:phase=modifiers?Phase::KeyUp:Phase::Release;break;
        case Phase::KeyUp:phase=Phase::Release;break;
        case Phase::Release:phase=Phase::Done;break;
        default:break;
        }
    }
};
} // namespace sys::hid
