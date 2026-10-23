#pragma once
#include <array>
#include <cstdint>

namespace sys {
struct TimerKey {
    enum Action : uint8_t { Key, Power, Home, Overflow } action=Key;
    int64_t timeUs=0;
};
// Called under button_mgr's critical section. No allocation or GUI callbacks.
// Capture the first press's owner until PRESS_END, including delayed doubles.
class TimerKeyState {
public:
    enum Key : uint8_t { Boot, Start, Pause };
    enum Event : uint8_t { Down, Up, End, Single, Double, Long };
    static constexpr unsigned Capacity=32;
    uint32_t claim() {
        if(!++generation)++generation;
        session=generation;head=used=0;home=overflow=false;return session;
    }
    void release(uint32_t token) {
        if(token && token==session){session=0;head=used=0;home=overflow=false;}
    }
    bool take(uint32_t token,TimerKey &out) {
        if(!token || token!=session)return false;
        if(home){home=false;out={TimerKey::Home,homeAt};return true;}
        if(used){out=queue[head];head=(head+1)%Capacity;--used;return true;}
        if(overflow){overflow=false;out={TimerKey::Overflow,0};return true;}
        return false;
    }
    bool route(Key key,Event event,int64_t now,bool interactive=true) {
        auto &press=presses[key];
        if(event==Down) {
            if(!press.sequence){press.sequence=true;press.owner=session;}
            press.held=true;press.longPress=false;
        }
        const bool consumed=session || press.owner;
        if(event==Long)press.longPress=true;
        if(event==Up) {
            if(press.held && !press.longPress && press.owner && press.owner==session && interactive && key!=Boot)
                push({key==Start?TimerKey::Key:TimerKey::Power,now});
            press.held=false;
        }
        if(event==Double && key==Boot && press.owner && press.owner==session && interactive) {
            home=true;homeAt=now; // Exit remains available even when the input queue is full.
        }
        if(event==End)press={};
        // Preserve the emergency long-PWR callback and screen/sleep recovery.
        if(key==Pause && (event==Long || !interactive))return false;
        return consumed;
    }
private:
    struct Press {uint32_t owner=0;bool sequence=false,held=false,longPress=false;};
    uint32_t session=0,generation=0;
    unsigned head=0,used=0;
    bool home=false,overflow=false;
    int64_t homeAt=0;
    std::array<Press,3> presses{};
    std::array<TimerKey,Capacity> queue{};
    void push(TimerKey input) {
        if(used==Capacity){overflow=true;return;}
        queue[(head+used)%Capacity]=input;++used;
    }
};
}
