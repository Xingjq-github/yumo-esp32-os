#pragma once
#include "mp3_model.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>

namespace core::mp3 {
struct Entry {std::string path,title;};
struct Listing {
    std::atomic<bool> cancel{false},done{false};
    sd::Access access;std::vector<Entry> entries;std::string error;bool truncated=false;
};
enum class State:uint8_t {Loading,Playing,Paused,Seeking,Ended,Failed};
struct Player {
    std::atomic<bool> cancel{false},done{false},play{true},metadataReady{false};
    std::atomic<State> state{State::Loading};
    std::atomic<uint32_t> position{0},duration{0},seekMs{UINT32_MAX},level{0};
    std::atomic<unsigned> rate{0},bitrate{0},outputRate{0};
    std::atomic<unsigned> underruns{0}; // Output starvation/rebuffer count.
    sd::Access access;std::string path,error;
    std::vector<Lyric> lyrics; // Immutable after metadataReady (release/acquire).
};
std::shared_ptr<Listing> scan();
std::shared_ptr<Player> start(const std::string &path);
bool idle();
bool storageIdle();
}
