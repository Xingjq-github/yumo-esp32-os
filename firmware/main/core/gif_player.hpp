#pragma once
#include "sd_media.hpp"
#include <atomic>
#include <memory>
#include <string>
namespace core::gif {
bool isFile(const std::string &path);
struct Job {
    std::atomic<bool> cancelled{false},done{false};
    std::atomic<int> ready{-1},displayed{-1};
    std::string path;
    sd::Access access;
    uint16_t *pixels[2]{};
    unsigned width=0,height=0;
    // Written with pixels by the worker, published through ready's release.
    unsigned delayMs=100;
    const char *error=nullptr;
    // GUI only. Keep the animation timeline across frames (and loop boundaries)
    // so decode time and timer jitter do not get added to every frame's delay.
    int readyFrame(uint32_t now) const {
        const int frame=ready.load(std::memory_order_acquire);
        return frame>=0 && (!clockStarted || int32_t(now-nextAt)>=0)?frame:-1;
    }
    // Call after detaching the previous image from LVGL. Clearing ready releases
    // that old buffer to the worker; the new displayed buffer stays read-only.
    void present(int frame,uint32_t now) {
        // Keep small polling jitter out of the timeline, but rebase if decoding
        // or a stalled GUI fell a whole frame behind. Do not burst stale frames.
        if(!clockStarted || int32_t(now-nextAt)>=int32_t(delayMs))nextAt=now;
        nextAt+=delayMs;
        clockStarted=true;
        displayed.store(frame,std::memory_order_release);
        ready.store(-1,std::memory_order_release);
    }
    ~Job();
private:
    uint32_t nextAt=0;
    bool clockStarted=false;
};
std::shared_ptr<Job> start(const std::string &path);
bool idle();
}
