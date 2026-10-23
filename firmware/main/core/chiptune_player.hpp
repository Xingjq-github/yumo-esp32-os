#pragma once
#include "chiptune_score.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
#include <mutex>
namespace core::chip {
inline constexpr const char *Directory="/sdcard/chiptune";
struct Entry {std::string path,name,title,error;bool inspected=false;};
struct Listing {
    std::atomic<bool> cancel{false},done{false};
    sd::Access access;
    std::vector<Entry> entries;
    std::string error;
    bool truncated=false;
    bool metadataOnly=false;
    size_t offset=0;
};
enum class State:uint8_t {Loading,Playing,Paused,Stopped,Failed};
struct Player {
    std::atomic<bool> cancel{false},done{false},play{true},rewind{false},loop{false};
    std::atomic<State> state{State::Loading};
    std::atomic<unsigned> position{0},duration{0},tempo{0};
    sd::Access access;
    std::string path,title,error; // Published once, before state leaves Loading.
    std::mutex wakeMutex;
    void *worker=nullptr; // Protected by wakeMutex; cleared before task deletion.
    void wake();
};
std::shared_ptr<Listing> scan();
std::shared_ptr<Listing> titles(const std::vector<Entry> &,size_t offset);
std::shared_ptr<Player> start(const std::string &path);
bool idle(); // Audio worker completely drained; safe to hand off codec.
bool busy(); // Loading/playing; paused or stopped can enter automatic sleep.
bool storageIdle(); // File operations only; loaded playback does not hold SD.
}
