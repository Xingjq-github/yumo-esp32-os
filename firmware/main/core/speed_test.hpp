#pragma once
#include "speed_test_model.hpp"
#include "speed_test_store.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
#include <mutex>

namespace core::speed {
struct Snapshot {
    Record record;
    Phase phase=Phase::Preparing;
    unsigned sample=0;
    uint64_t currentBytes=0;
    int64_t directionElapsedUs=0;
    // Rolling transfer observations (kbps), provisional until server validation.
    // -1 marks a new request; missing observations must never become zero speed.
    std::array<int32_t,40> liveKbps{};
    unsigned liveCount=0;
    double currentMbps=0;
    bool liveValid=false;
    bool saved=false;
    char filename[48]{};
    int storageError=0;
};
struct Job {
    std::atomic<bool> cancelled{false},done{false};
    std::mutex mutex;
    Snapshot published;
    sd::Access sdAccess;
    Snapshot snapshot() {std::lock_guard<std::mutex> lock(mutex);return published;}
    void snapshot(Snapshot &out) {std::lock_guard<std::mutex> lock(mutex);out=published;}
};
struct HistoryJob {
    std::atomic<bool> cancelled{false},done{false};
    sd::Access sdAccess;
    std::string before;
    uint64_t cardGeneration=0;
    store::Page page; // Read only after done(acquire).
};
struct DeleteJob {
    std::atomic<bool> cancelled{false},done{false};
    sd::Access sdAccess;
    store::Entry entry;
    uint64_t cardGeneration=0;
    int error=0; // Read only after done(acquire).
};
std::shared_ptr<Job> start();
std::shared_ptr<HistoryJob> history(const std::string &before={});
std::shared_ptr<DeleteJob> removeHistory(const store::Entry &entry,uint64_t cardGeneration);
bool idle();
} // namespace core::speed
