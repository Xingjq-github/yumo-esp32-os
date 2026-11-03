#include "nvs_capacity_host.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_rom_crc.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {
size_t checks = 0, scenarios = 0, cuts = 0;
size_t gcErases = 0, maximumUsed = 0;
std::string outputDir;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)
#define OK(expression) CHECK((expression) == ESP_OK)

std::vector<uint8_t> blob(size_t size, uint32_t seed) {
    std::vector<uint8_t> out(size);
    for (auto& byte : out) { seed = seed * 1664525u + 1013904223u; byte = uint8_t(seed >> 24); }
    return out;
}
nvs_handle_t open(const char* name = "fixture") {
    nvs_handle_t handle = 0;
    OK(nvs_open(name, NVS_READWRITE, &handle));
    return handle;
}
std::vector<uint8_t> read(nvs_handle_t handle, const char* name) {
    size_t length = 0;
    OK(nvs_get_blob(handle, name, nullptr, &length));
    std::vector<uint8_t> bytes(length);
    OK(nvs_get_blob(handle, name, bytes.data(), &length));
    CHECK(length == bytes.size());
    return bytes;
}
void save(const std::string& file) {
    const auto image = nvs_host::snapshot();
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), image.size());
    CHECK(out.good());
}
void basic() {
    ++scenarios;
    const uint8_t check[] = "123456789";
    CHECK(esp_rom_crc32_le(0, check, 9) == 0xcbf43926u);
    CHECK(esp_rom_crc32_le(esp_rom_crc32_le(0, check, 4), check + 4, 5) == 0xcbf43926u);
    OK(nvs_host::reset());
    auto h = open("nimble_bond");
    const auto security = blob(88, 1);
    OK(nvs_set_blob(h, "our_sec_1", security.data(), security.size()));
    nvs_stats_t stats{};
    OK(nvs_get_stats(nullptr, &stats));
    CHECK(stats.total_entries == 504);
    CHECK(stats.used_entries == 6); // namespace + blob-data3 + blob-index2
    CHECK(stats.available_entries == 372); // one reserve sector
    OK(nvs_set_u8(h, "u8", 203));
    OK(nvs_set_u32(h, "u32", 0xfedcba98u));
    OK(nvs_set_str(h, "text", "Yumo synthetic NVS fixture"));
    OK(nvs_commit(h));
    const auto oldHandle = h;
    auto other = open("system");
    OK(nvs_set_u32(other, "keep", 0x34567890));
    nvs_close(other);
    OK(nvs_host::reboot());
    uint8_t u8 = 0;
    CHECK(nvs_get_u8(oldHandle, "u8", &u8) == ESP_ERR_NVS_INVALID_HANDLE);
    h = open("nimble_bond");
    CHECK(read(h, "our_sec_1") == security);
    OK(nvs_get_u8(h, "u8", &u8)); CHECK(u8 == 203);
    uint32_t u32 = 0;
    OK(nvs_get_u32(h, "u32", &u32)); CHECK(u32 == 0xfedcba98u);
    char text[64]{}; size_t textSize = sizeof(text);
    OK(nvs_get_str(h, "text", text, &textSize));
    CHECK(std::strcmp(text, "Yumo synthetic NVS fixture") == 0);
    OK(nvs_erase_key(h, "our_sec_1"));
    OK(nvs_commit(h));
    OK(nvs_host::reboot());
    h = open("nimble_bond"); size_t n = 0;
    CHECK(nvs_get_blob(h, "our_sec_1", nullptr, &n) == ESP_ERR_NVS_NOT_FOUND);
    other = open("system");
    OK(nvs_get_u32(other, "keep", &u32)); CHECK(u32 == 0x34567890);
    nvs_close(h); nvs_close(other);
}
void capacity() {
    ++scenarios;
    OK(nvs_host::reset());
    auto h = open();
    const auto value = blob(88, 2);
    esp_err_t result = ESP_OK;
    size_t count = 0;
    for (; count < 1000; ++count) {
        char key[16]; std::snprintf(key, sizeof(key), "key%zu", count);
        result = nvs_set_blob(h, key, value.data(), value.size());
        if (result != ESP_OK) break;
    }
    CHECK(result == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    CHECK(count > 40 && count < 100);
    nvs_stats_t stats{}; OK(nvs_get_stats(nullptr, &stats));
    maximumUsed = stats.used_entries;
    OK(nvs_host::reboot()); h = open();
    for (size_t i = 0; i < count; ++i) {
        char key[16]; std::snprintf(key, sizeof(key), "key%zu", i);
        CHECK(read(h, key) == value);
    }
    for (size_t i = 0; i < count; i += 2) {
        char key[16]; std::snprintf(key, sizeof(key), "key%zu", i);
        OK(nvs_erase_key(h, key));
    }
    const auto replacement = blob(1200, 5);
    OK(nvs_set_blob(h, "reused", replacement.data(), replacement.size()));
    CHECK(read(h, "reused") == replacement);
    nvs_close(h);
}
void churn() {
    ++scenarios;
    OK(nvs_host::reset());
    auto h = open();
    const auto sentinel = blob(1400, 9);
    OK(nvs_set_blob(h, "sentinel", sentinel.data(), sentinel.size()));
    nvs_host::clearIo();
    for (size_t i = 0; i < 1000; ++i) {
        const auto value = blob(88 + i % 1000, uint32_t(i));
        OK(nvs_set_blob(h, "changing", value.data(), value.size()));
        if (i % 3 == 0) {
            OK(nvs_set_u32(h, "transient", uint32_t(i)));
            OK(nvs_erase_key(h, "transient"));
        }
        if (i % 17 == 0) {
            OK(nvs_host::reboot()); h = open();
        }
        CHECK(read(h, "changing") == value);
        CHECK(read(h, "sentinel") == sentinel);
    }
    gcErases = nvs_host::ioStats().erases;
    CHECK(gcErases > 50);
    CHECK(nvs_host::ioStats().forbiddenBitTransitions == 0);
    nvs_close(h);
}

// Atomicity at every mutation boundary and representative partial-byte cuts.
// Values must be complete old/new (or absent for creation/deletion), and an
// unrelated key must survive. This runs real page-manager recovery on reboot.
void sweep(const nvs_host::Snapshot& before, const std::vector<uint8_t>& oldValue,
           const std::vector<uint8_t>& newValue, bool deletion, bool creation,
           bool requireErase) {
    const auto sentinel = blob(1400, 9);
    OK(nvs_host::restore(before));
    auto h = open();
    nvs_host::clearIo();
    if (deletion) OK(nvs_erase_key(h, "changing"));
    else OK(nvs_set_blob(h, "changing", newValue.data(), newValue.size()));
    const auto ops = nvs_host::operations();
    CHECK(!ops.empty());
    if (requireErase) CHECK(nvs_host::ioStats().erases > 0);
    nvs_close(h);
    for (size_t op = 0; op < ops.size(); ++op) {
        const auto len = ops[op].requestedBytes;
        const std::set<size_t> prefixes = {0, 1, len / 2, len - 1, len};
        for (const size_t prefix : prefixes) {
            ++scenarios; ++cuts;
            OK(nvs_host::restore(before)); h = open();
            nvs_host::clearIo(); nvs_host::armPowerCut(op, prefix);
            bool cut = false;
            try {
                if (deletion) (void)nvs_erase_key(h, "changing");
                else (void)nvs_set_blob(h, "changing", newValue.data(), newValue.size());
            } catch (const nvs_host::PowerCut&) { cut = true; }
            CHECK(cut);
            if (!creation && !deletion && !requireErase && op == 4 && prefix == len) {
                save(outputDir + "/update-cut.bin");
                std::ofstream out(outputDir + "/update-before.bin", std::ios::binary);
                out.write(reinterpret_cast<const char*>(before.data()), before.size());
            }
            const auto rc = nvs_host::reboot();
            if (rc != ESP_OK) std::fprintf(stderr,
                "reboot rc=%d op=%zu/%zu kind=%d prefix=%zu/%zu\n", rc, op, ops.size(),
                int(ops[op].kind), prefix, len);
            OK(rc); h = open();
            CHECK(nvs_host::ioStats().deinitMutations == 0);
            CHECK(read(h, "sentinel") == sentinel);
            size_t size = 0;
            const auto found = nvs_get_blob(h, "changing", nullptr, &size);
            if (found == ESP_ERR_NVS_NOT_FOUND) {
                if (!creation && !deletion) std::fprintf(stderr,
                    "missing replacement op=%zu/%zu kind=%d offset=%zu prefix=%zu/%zu gc=%d\n",
                    op, ops.size(), int(ops[op].kind), ops[op].offset, prefix, len, int(requireErase));
                if (!creation && !deletion) save(outputDir + "/update-after.bin");
                CHECK(creation || deletion);
            }
            else {
                OK(found);
                const auto value = read(h, "changing");
                if (deletion) CHECK(value == oldValue);
                else if (creation) CHECK(value == newValue);
                else CHECK(value == oldValue || value == newValue);
            }
            // After recovery the partition must still accept later writes.
            OK(nvs_set_u32(h, "after_reboot", 0x12345678));
            nvs_close(h);
        }
    }
}
void powerLoss() {
    OK(nvs_host::reset());
    auto h = open();
    const auto sentinel = blob(1400, 9), initial = blob(1500, 111), next = blob(1700, 222);
    OK(nvs_set_blob(h, "sentinel", sentinel.data(), sentinel.size()));
    const auto empty = nvs_host::snapshot();
    nvs_close(h);
    sweep(empty, {}, initial, false, true, false);
    OK(nvs_host::restore(empty)); h = open();
    OK(nvs_set_blob(h, "changing", initial.data(), initial.size()));
    const auto baseline = nvs_host::snapshot();
    nvs_close(h);
    sweep(baseline, initial, next, false, false, false);
    sweep(baseline, initial, {}, true, false, false);

    // The SDK duplicate-recovery defect also affected tiny blobs. Exercise
    // both version offsets, including a second update toggling 128 back to 0.
    const auto tiny0 = blob(8, 71), tiny1 = blob(8, 72), tiny2 = blob(8, 73);
    OK(nvs_host::restore(empty)); h = open();
    OK(nvs_set_blob(h, "changing", tiny0.data(), tiny0.size()));
    auto tinyBaseline = nvs_host::snapshot(); nvs_close(h);
    sweep(tinyBaseline, tiny0, tiny1, false, false, false);
    OK(nvs_host::restore(tinyBaseline)); h = open();
    OK(nvs_set_blob(h, "changing", tiny1.data(), tiny1.size()));
    tinyBaseline = nvs_host::snapshot(); nvs_close(h);
    sweep(tinyBaseline, tiny1, tiny2, false, false, false);

    // Locate a real GC transition; sweep its page-copy and sector-erase steps.
    OK(nvs_host::restore(baseline)); h = open();
    auto old = initial;
    bool foundGc = false;
    for (size_t i = 0; i < 100; ++i) {
        const auto before = nvs_host::snapshot();
        const auto value = blob(1500 + i % 100, uint32_t(i + 333));
        nvs_host::clearIo();
        OK(nvs_set_blob(h, "changing", value.data(), value.size()));
        if (nvs_host::ioStats().erases != 0) {
            nvs_close(h);
            sweep(before, old, value, false, false, true);
            foundGc = true;
            break;
        }
        old = value;
    }
    CHECK(foundGc);
}
void returnedFailure() {
    ++scenarios;
    OK(nvs_host::reset());
    auto h = open(); const auto first = blob(100, 1), next = blob(100, 2);
    OK(nvs_set_blob(h, "value", first.data(), first.size()));
    nvs_host::armFailure(0);
    CHECK(nvs_set_blob(h, "value", next.data(), next.size()) != ESP_OK);
    OK(nvs_host::reboot()); h = open();
    CHECK(read(h, "value") == first);
    OK(nvs_set_blob(h, "value", next.data(), next.size()));
    CHECK(read(h, "value") == next);
    nvs_close(h);
}
} // namespace

int main(int argc, char** argv) {
    const std::string output = argc > 1 ? argv[1] : ".";
    outputDir = output;
    basic(); capacity(); churn(); powerLoss(); returnedFailure();
    save(output + "/final-flash.bin");
    const std::string report = "{\"passed\":true,\"partition_bytes\":16384,\"scenarios\":" +
        std::to_string(scenarios) + ",\"checks\":" + std::to_string(checks) +
        ",\"power_cuts\":" + std::to_string(cuts) + ",\"churn_gc_erases\":" +
        std::to_string(gcErases) + ",\"full_partition_used_entries\":" +
        std::to_string(maximumUsed) + "}";
    std::ofstream result(output + "/result.json"); result << report << '\n';
    std::printf("%s\n", report.c_str());
    return 0;
}
