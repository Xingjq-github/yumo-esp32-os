#pragma once
#include "garden_store.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
namespace core::garden {
struct Job {
    bool saving=false;
    State state;
    sd::Access access;
    uint64_t generation=0,expected=0;
    store::Snapshot result;
    int error=0;
    std::atomic<bool> cancelled{false},done{false};
};
std::shared_ptr<Job> open();
std::shared_ptr<Job> save(const State &state,uint64_t revision,uint64_t generation,sd::Access access);
bool idle();
}
