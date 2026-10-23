#include "core/storage_usage.hpp"
constexpr auto psram=core::memoryUsage(8*1024*1024,3747*1024,1700*1024);
static_assert(psram.fixed+psram.used+psram.free==psram.total);
static_assert(psram.fixed==(8192-3747)*1024 && psram.used==(3747-1700)*1024);
static_assert(psram.free==1700*1024);
constexpr auto empty=core::memoryUsage(0,1024,1024);
static_assert(empty.fixed==0 && empty.used==0 && empty.free==0);
constexpr auto bounded=core::memoryUsage(512*1024,600*1024,700*1024);
static_assert(bounded.free==512*1024 && bounded.used==0 && bounded.fixed==0);
static_assert(core::storageEdge(0,0,212)==0);
static_assert(core::storageEdge(128ull*1024*1024*1024,128ull*1024*1024*1024,212)==212);
static_assert(core::storageEdge(70,100,212)+
    (core::storageEdge(100,100,212)-core::storageEdge(70,100,212))==212);
static_assert(0x20000+0x950000==0x970000);
static_assert(0x950000+0x550000+0x100000+0x60000==16*1024*1024);
