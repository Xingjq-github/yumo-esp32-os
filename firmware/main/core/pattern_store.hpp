#pragma once
#include "pattern_format.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
namespace core::patterns {
inline constexpr const char *Directory=sd::Patterns;
inline constexpr const char *LessonPath="/sdcard/patterns/lessons.ypg";
inline constexpr const char *ProgressPath="/sdcard/patterns/progress.dat";
enum class Operation {Open,Read,Save};
struct Job {
    Operation operation=Operation::Open;sd::Access access;
    Progress progress;std::unique_ptr<Lesson> lesson;
    unsigned index=0;uint64_t generation=0;
    int error=0;bool fallback=false,damaged=false;
    std::atomic<bool> cancelled{false},done{false};
};
std::shared_ptr<Job> open();
std::shared_ptr<Job> read(unsigned index,uint64_t generation);
std::shared_ptr<Job> save(const Progress &,uint64_t generation,sd::Access);
bool idle();
}
