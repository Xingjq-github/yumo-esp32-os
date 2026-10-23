#pragma once
#include "sd_media.hpp"
#include "photo_view.h"
#include <atomic>
#include <memory>
#include <string>
#include <cstdint>

namespace core::photo {
struct Source {
    // Small files may be cached. Large files retain only immutable identity.
    uint8_t *data=nullptr;
    std::string path;
    size_t length=0;
    int64_t modified=0;
    uint64_t generation=0,fingerprint=0;
    uint16_t width=0,height=0;
    bool png=false,progressive=false;
    ~Source();
};
struct Job {
    std::atomic<bool> cancelled{false},done{false};
    std::shared_ptr<Source> source;
    sd::Access sdAccess;
    std::string path;
    const char *error=nullptr;
    uint8_t *pixels=nullptr;
    uint16_t width=0,height=0,targetWidth=0,fitWidth=0;
    int32_t panX=0,panY=0;
    photo_view view{};
    bool cacheHit=false;
    ~Job();
};
// One on-demand task. No App/LVGL pointer; caller consumes only after done(acquire).
// A zero targetWidth fits 240x240. Streaming zoom reopens and verifies the SD
// file in a fresh Session; cached small files do not need another SD read.
std::shared_ptr<Job> start(const std::string &path,std::shared_ptr<Source> source={},uint16_t targetWidth=0,
                          int32_t panX=0,int32_t panY=0);
bool idle();
}
