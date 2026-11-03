#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace sys::hid {
// Bounded relative-motion backlog. Adjacent motion at the same button state
// coalesces; button transitions retain their order. Excess motion saturates
// at a fixed backlog instead of allocating memory for every touch sample.
// The caller serializes every method; no allocation or callback occurs here.
class MouseQueue {
public:
    static constexpr unsigned Capacity=16;
    static constexpr int MaxDelta=4096;
    static constexpr unsigned ClickHoldMs=20;
    struct Ticket {
        std::array<uint8_t,4> bytes{};
        uint32_t revision=0,id=0;
        unsigned dwellMs=0;
        bool forced=false,valid=false;
    };
    bool push(int dx,int dy,int wheel,uint8_t buttons,unsigned dwell=0) {
        buttons&=7;
        if(count_) {
            auto &tail=slots_[(head_+count_-1)%Capacity];
            if(tail.buttons==buttons && !tail.dwell && !dwell) {
                tail.dx=bounded(int64_t(tail.dx)+dx);tail.dy=bounded(int64_t(tail.dy)+dy);
                tail.wheel=bounded(int64_t(tail.wheel)+wheel);return true;
            }
        } else if(!dx&&!dy&&!wheel&&buttons==sentButtons_&&!release_&&!inFlight_&&!dwell)return true;
        // Always leave room for the release of an accepted press/drag.
        if(count_>=Capacity-(buttons?1u:0u)) {
            if(buttons)return false;
            cancel(); // discard unsent work, but never discard the all-up
            return true;
        }
        auto &entry=slots_[(head_+count_)%Capacity];
        entry={bounded(dx),bounded(dy),bounded(wheel),buttons,dwell,++nextId_};
        ++count_;return true;
    }
    bool click(uint8_t button) {
        if(!button || (button&~7u) || count_>Capacity-3)return false;
        // Reserve both edges atomically before the caller schedules anything.
        push(0,0,0,button,ClickHoldMs);push(0,0,0,0);return true;
    }
    Ticket begin() {
        if(inFlight_ || (!release_&&!count_))return {};
        inFlight_=true;
        if(release_)return {{0,0,0,0},revision_,0,0,true,true};
        const auto &e=slots_[head_];
        return {{e.buttons,uint8_t(step(e.dx)),uint8_t(step(e.dy)),uint8_t(step(e.wheel))},
                revision_,e.id,e.dwell,false,true};
    }
    void complete(const Ticket &ticket,bool accepted) {
        if(!ticket.valid)return;
        inFlight_=false;
        if(!accepted)return;
        sentButtons_=ticket.bytes[0];
        if(ticket.revision!=revision_) {
            // Cancellation raced with the send: finish the all-up first.
            release_=sentButtons_!=0;return;
        }
        if(ticket.forced){release_=false;return;}
        if(!count_ || slots_[head_].id!=ticket.id)return;
        auto &e=slots_[head_];
        e.dx-=int8_t(ticket.bytes[1]);e.dy-=int8_t(ticket.bytes[2]);e.wheel-=int8_t(ticket.bytes[3]);
        if(!e.dx&&!e.dy&&!e.wheel) {e={};head_=(head_+1)%Capacity;--count_;}
    }
    void cancel() {
        ++revision_;slots_={};head_=count_=0;
        release_=sentButtons_!=0 || inFlight_;
    }
    void reset() {
        ++revision_;slots_={};head_=count_=0;sentButtons_=0;
        release_=inFlight_=false;
    }
    bool pending() const {return count_||release_||inFlight_;}
    bool idle() const {return !pending()&&!sentButtons_;}
    bool held() const {return sentButtons_!=0;}
    unsigned size() const {return count_;}
private:
    struct Entry {int dx=0,dy=0,wheel=0;uint8_t buttons=0;unsigned dwell=0;uint32_t id=0;};
    static int bounded(int64_t value){return int(std::max<int64_t>(-MaxDelta,std::min<int64_t>(MaxDelta,value)));}
    static int step(int value){return std::max(-127,std::min(127,value));}
    std::array<Entry,Capacity> slots_{};
    unsigned head_=0,count_=0;
    uint32_t revision_=1,nextId_=0;
    uint8_t sentButtons_=0;
    bool release_=false,inFlight_=false;
};
} // namespace sys::hid
