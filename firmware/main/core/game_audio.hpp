#pragma once
#include "game_synth.hpp"
#include "worker_wake.hpp"
#include <atomic>
#include <array>
#include <memory>
namespace core::game_audio {
struct Session {
    explicit Session(Track selected=Track::Runner):track(selected){}
    const Track track;
    WorkerWake wake;
    void requestStop() {stop.store(true);wake.notify();}
    void setMusic(bool value) {if(music.exchange(value)!=value)wake.notify();}
    void trigger(unsigned value) {effects.fetch_or(value);wake.notify();}
    void setMorseReady(bool value) {if(morseReady.exchange(value)!=value)wake.notify();}
    void setMorseKey(bool value) {if(morseKey.exchange(value)!=value)wake.notify();}
    std::atomic<bool> stop{false},music{false};
    std::atomic<unsigned> effects{0},motion{0};
    std::atomic<bool> morseReady{false},morseKey{false};
    struct MorseCommand {uint32_t code=0,epoch=0;};
    std::array<MorseCommand,16> morseQueue{};
    std::atomic<unsigned> morseHead{0},morseTail{0};
    std::atomic<uint32_t> morseReset{0};
    // One GUI producer, one audio consumer; reset invalidates commands without
    // moving the other thread's index or reusing a slot it is still reading.
    bool pushMorse(uint32_t code) {
        const auto head=morseHead.load(std::memory_order_relaxed);
        if(head-morseTail.load(std::memory_order_acquire)>=morseQueue.size())return false;
        morseQueue[head%morseQueue.size()]={code,morseReset.load()};
        morseHead.store(head+1,std::memory_order_release);wake.notify();return true;
    }
    bool popMorse(MorseCommand &command) {
        const auto tail=morseTail.load(std::memory_order_relaxed);
        if(tail==morseHead.load(std::memory_order_acquire))return false;
        command=morseQueue[tail%morseQueue.size()];
        morseTail.store(tail+1,std::memory_order_release);return true;
    }
    std::atomic<bool> failed{false};
    void pause(bool notify=true){music.store(false);motion.store(0);effects.store(Silence);morseKey.store(false);morseReady.store(false);morseReset.fetch_add(1);if(notify)wake.notify();}
};
std::shared_ptr<Session> start(Track track=Track::Runner);
bool idle();
bool busy(); // Actual speaker activity; a silent menu may enter automatic sleep.
}
