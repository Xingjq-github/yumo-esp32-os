#pragma once
#include "file_operations.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
namespace core::files {
struct Job {
    Request request;
    Result result;
    sd::Access access;
    uint64_t generation=0;
    std::atomic<bool> cancelled{false},done{false};
    std::atomic<uint32_t> kilobytes{0},changed{0};
};
std::shared_ptr<Job> start(Request request,uint64_t generation=0);
bool idle();
const char *errorText(int error);
}
