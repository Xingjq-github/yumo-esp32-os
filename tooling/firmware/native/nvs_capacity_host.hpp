#pragma once

// Deterministic host flash for the unmodified ESP-IDF NVS implementation.
// Only the partition/OS boundary is simulated; use the real nvs_* C API.
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>
#include "esp_err.h"

namespace nvs_host {
constexpr size_t FlashSize = 16 * 1024;
constexpr size_t SectorSize = 4096;
constexpr size_t EntireOperation = std::numeric_limits<size_t>::max();
using Snapshot = std::array<uint8_t, FlashSize>;

enum class OperationKind { Write, Erase };
struct Operation {
    OperationKind kind;
    size_t offset;
    size_t requestedBytes;
    size_t appliedBytes;
};
struct IoStats {
    size_t reads = 0;
    size_t readBytes = 0;
    size_t writes = 0;
    size_t writeBytes = 0;
    size_t erases = 0;
    size_t eraseBytes = 0;
    size_t forbiddenBitTransitions = 0;
    size_t deinitMutations = 0;
};
class PowerCut final : public std::exception {
public:
    const char* what() const noexcept override { return "simulated NVS power cut"; }
};

// Recreates NVS runtime and its real handle table. All previous handles expire.
esp_err_t reset();
esp_err_t reboot();
Snapshot snapshot();
esp_err_t restore(const Snapshot& image);

// Fault indexing starts at zero after arming: cut/fail operation N after N
// successful write/erase calls. bytesBeforeCut is a deterministic prefix;
// EntireOperation cuts after the physical operation, before returning to NVS.
// PowerCut unwinds the host call immediately; reboot() clears the fault.
// Flash failure returns ESP_ERR_FLASH_OP_FAIL on this and all later mutations.
void armPowerCut(size_t mutationIndex, size_t bytesBeforeCut = 0);
void armFailure(size_t mutationIndex, size_t bytesBeforeFailure = 0);
void clearFault();
void clearIo();
const IoStats& ioStats();
const std::vector<Operation>& operations();
} // namespace nvs_host
