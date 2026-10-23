#pragma once
#include <cstdint>

namespace sys {
// Protected by the button manager's short critical section. These bits match
// NES A/B, but this state owns only physical keys, never an App or a worker.
class GameKeyState {
public:
    static constexpr uint8_t Key=1, Boot=2;
    constexpr uint32_t claim() {
        generation_=generation_==0x7fffffffu?1:generation_+1;
        session_=generation_; gameplay_=running_=false;
        held_=pending_=0; captured_|=raw_;
        return session_;
    }
    constexpr void release(uint32_t session) {
        if(!session || session!=session_) return;
        session_=0; gameplay_=running_=false; held_=pending_=0;
        // Keep the gesture fence until PRESS_END consumes delayed clicks.
    }
    constexpr void mode(uint32_t session,bool gameplay) {
        if(!session || session!=session_ || gameplay_==gameplay) return;
        gameplay_=gameplay; running_=false; held_=pending_=0;
        if(gameplay && (raw_&Boot)) bootGesture_=true;
    }
    constexpr void running(uint32_t session,bool enabled) {
        if(!session || session!=session_) return;
        enabled=enabled && gameplay_;
        if(running_!=enabled) { running_=enabled; held_=pending_=0; }
    }
    constexpr void clear(uint32_t session) {
        if(session && session==session_) held_=pending_=0;
    }
    constexpr void down(uint8_t key) {
        raw_|=key;
        if(session_) captured_|=key;
        if(key==Boot && gameplay_) bootGesture_=true;
        if(session_ && gameplay_ && running_) { held_|=key; pending_|=key; }
    }
    constexpr void up(uint8_t key) { raw_&=uint8_t(~key); held_&=uint8_t(~key); }
    constexpr void end(uint8_t key) {
        captured_&=uint8_t(~key);
        if(key==Boot) bootGesture_=false;
    }
    constexpr bool consumeClick(uint8_t key) const { return session_ || (captured_&key); }
    constexpr bool blockHome() const { return gameplay_ || bootGesture_; }
    constexpr uint8_t take(uint32_t session) {
        if(!session || session!=session_ || !gameplay_ || !running_) return 0;
        const uint8_t value=held_|pending_; pending_=0;
        return value; // A short down/up between frames still lasts one frame.
    }
private:
    uint32_t session_=0,generation_=0;
    uint8_t raw_=0,held_=0,pending_=0,captured_=0;
    bool gameplay_=false,running_=false,bootGesture_=false;
};
}
