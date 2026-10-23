#pragma once
#include "vault_crypto.hpp"
namespace vault {
struct Disk {
    Bytes bundle, config;
    uint32_t generation = 0;
    int slot = -1;
    bool blank = false;
};
// All methods worker-only, serialized by the vault job owner. Never touch NVS,
// assets, app partitions or the reserved last 256KiB.
bool load(Disk &disk, std::string &error, const std::atomic<bool> &cancel);
bool commit(const Disk &previous, const Bytes &bundle, const Bytes &config, std::string &error,
            const std::atomic<bool> &cancel);
} // namespace vault
