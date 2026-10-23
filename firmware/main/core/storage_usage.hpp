#pragma once
#include <algorithm>
#include <cstdint>

namespace core {
// Physical memory is not the heap size. Keep fixed/mapped bytes visible.
struct MemoryUsage {
    uint64_t total=0, fixed=0, used=0, free=0;
};
constexpr MemoryUsage memoryUsage(uint64_t physical,uint64_t heap,uint64_t available) {
    const auto pool=std::min(physical,heap);
    const auto free=std::min(pool,available);
    return {physical,physical-pool,pool-free,free};
}
// Monotone cumulative edges keep a small bar exact, including tiny regions.
constexpr int storageEdge(uint64_t bytes,uint64_t total,int width) {
    return total && width>0?int(std::min(bytes,total)*uint64_t(width)/total):0;
}
struct StorageUsage {
    uint64_t flash=0, firmware=0, image=0, assets=0, vault=0;
    uint64_t sdPhysical=0, sdTotal=0, sdFree=0;
    bool imageValid=false, sdValid=false;
};
} // namespace core
