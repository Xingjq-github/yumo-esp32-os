#include "nvs_capacity_host.hpp"
#include "nvs_flash.h"
#include "nvs_partition_manager.hpp"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include <algorithm>
#include <cstring>

namespace {
nvs_host::Snapshot flash = [] {
    nvs_host::Snapshot bytes{};
    bytes.fill(0xff);
    return bytes;
}();
const esp_partition_t partition = {0, nvs_host::FlashSize, "nvs", false, false};
nvs_host::IoStats stats;
std::vector<nvs_host::Operation> trace;
enum class Fault { None, Cut, Fail };
Fault fault = Fault::None;
size_t remaining = 0;
size_t faultPrefix = 0;
bool poweredOff = false;
bool valid(const esp_partition_t* p, size_t offset, size_t length) {
    return p == &partition && offset <= flash.size() && length <= flash.size() - offset;
}
esp_err_t mutate(nvs_host::OperationKind kind, size_t offset,
                 const uint8_t* bytes, size_t length) {
    if (poweredOff) throw nvs_host::PowerCut();
    const bool interrupted = fault != Fault::None && remaining == 0;
    if (fault != Fault::None && remaining != 0) --remaining;
    const size_t applied = interrupted ? std::min(length, faultPrefix) : length;
    trace.push_back({kind, offset, length, applied});
    if (kind == nvs_host::OperationKind::Write) {
        ++stats.writes;
        stats.writeBytes += applied;
        for (size_t i = 0; i < applied; ++i) {
            // NOR programming is bitwise AND, even after an incomplete write.
            // Report attempted 0->1 transitions rather than masking corruption.
            if ((flash[offset + i] & bytes[i]) != bytes[i])
                ++stats.forbiddenBitTransitions;
            flash[offset + i] &= bytes[i];
        }
    } else {
        ++stats.erases;
        stats.eraseBytes += applied;
        std::fill_n(flash.data() + offset, applied, uint8_t(0xff));
    }
    if (interrupted) {
        if (fault == Fault::Cut) {
            poweredOff = true;
            throw nvs_host::PowerCut();
        }
        return ESP_ERR_FLASH_OP_FAIL;
    }
    return ESP_OK;
}
} // namespace

extern "C" const esp_partition_t* esp_partition_find_first(
    esp_partition_type_t type, esp_partition_subtype_t subtype, const char* label) {
    if (type != ESP_PARTITION_TYPE_DATA || subtype != ESP_PARTITION_SUBTYPE_DATA_NVS)
        return nullptr;
    return !label || std::strcmp(label, partition.label) == 0 ? &partition : nullptr;
}
extern "C" esp_err_t esp_partition_read_raw(const esp_partition_t* p, size_t offset,
                                            void* data, size_t length) {
    if (!valid(p, offset, length) || (!data && length)) return ESP_ERR_INVALID_ARG;
    ++stats.reads;
    stats.readBytes += length;
    if (length) std::memcpy(data, flash.data() + offset, length);
    return ESP_OK;
}
extern "C" esp_err_t esp_partition_read(const esp_partition_t* p, size_t offset,
                                        void* data, size_t length) {
    return esp_partition_read_raw(p, offset, data, length);
}
extern "C" esp_err_t esp_partition_write_raw(const esp_partition_t* p, size_t offset,
                                             const void* data, size_t length) {
    if (!valid(p, offset, length) || (!data && length)) return ESP_ERR_INVALID_ARG;
    return mutate(nvs_host::OperationKind::Write, offset,
                  static_cast<const uint8_t*>(data), length);
}
extern "C" esp_err_t esp_partition_write(const esp_partition_t* p, size_t offset,
                                         const void* data, size_t length) {
    return esp_partition_write_raw(p, offset, data, length);
}
extern "C" esp_err_t esp_partition_erase_range(const esp_partition_t* p,
                                               size_t offset, size_t length) {
    if (!valid(p, offset, length) || offset % nvs_host::SectorSize || length % nvs_host::SectorSize)
        return ESP_ERR_INVALID_ARG;
    return mutate(nvs_host::OperationKind::Erase, offset, nullptr, length);
}

// LINUX_HOST_LEGACY_TEST omits ESP-IDF's platform flash-init entry points.
// These thin shims route directly into its unchanged partition manager.
extern "C" esp_err_t nvs_flash_init_partition(const char* name) {
    if (!name) return ESP_ERR_INVALID_ARG;
    return nvs::NVSPartitionManager::get_instance()->init_partition(name);
}
extern "C" esp_err_t nvs_flash_init() { return nvs_flash_init_partition("nvs"); }
extern "C" esp_err_t nvs_flash_erase_partition(const char* name) {
    if (!name || std::strcmp(name, "nvs")) return ESP_ERR_NOT_FOUND;
    (void)nvs_flash_deinit_partition(name);
    return esp_partition_erase_range(&partition, 0, flash.size());
}
extern "C" esp_err_t nvs_flash_erase() { return nvs_flash_erase_partition("nvs"); }
extern "C" esp_err_t nvs_flash_erase_partition_ptr(const esp_partition_t* p) {
    return p == &partition ? nvs_flash_erase() : ESP_ERR_INVALID_ARG;
}

namespace nvs_host {
void clearFault() { fault = Fault::None; remaining = faultPrefix = 0; }
void clearIo() { stats = {}; trace.clear(); }
esp_err_t reboot() {
    const auto mutations = trace.size();
    (void)nvs_flash_deinit();
    stats.deinitMutations += trace.size() - mutations;
    poweredOff = false;
    clearFault();
    return nvs_flash_init();
}
esp_err_t reset() {
    (void)nvs_flash_deinit();
    poweredOff = false;
    clearFault();
    flash.fill(0xff);
    clearIo();
    return nvs_flash_init();
}
Snapshot snapshot() { return flash; }
esp_err_t restore(const Snapshot& image) {
    (void)nvs_flash_deinit();
    poweredOff = false;
    clearFault();
    flash = image;
    clearIo();
    return nvs_flash_init();
}
void armPowerCut(size_t index, size_t prefix) {
    fault = Fault::Cut; remaining = index; faultPrefix = prefix;
}
void armFailure(size_t index, size_t prefix) {
    fault = Fault::Fail; remaining = index; faultPrefix = prefix;
}
const IoStats& ioStats() { return stats; }
const std::vector<Operation>& operations() { return trace; }
} // namespace nvs_host
