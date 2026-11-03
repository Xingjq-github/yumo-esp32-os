#include "yumo_ble_store.h"
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"
#include "ble_store_config_priv.h"
#include "esp_log.h"
#include "nvs.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Bound to the pinned SDK ABI. A future SDK/layout change must introduce a new
// decoder before it may reuse these on-flash keys. Secret bytes are never logged.
static_assert(MYNEWT_VAL(BLE_STATIC_TO_DYNAMIC), "dynamic NimBLE store required");
static_assert(MYNEWT_VAL(BLE_STORE_CONFIG_PERSIST), "persistent NimBLE store required");
static_assert(!MYNEWT_VAL(BLE_HOST_BASED_PRIVACY), "host privacy needs a separate migration");
static_assert(!MYNEWT_VAL(ENC_ADV_DATA), "EAD needs a separate migration");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_BONDS) == 16, "review capacity and format together");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_CCCDS) == 128, "review capacity and format together");
static_assert(sizeof(ble_addr_t) == 7 && sizeof(ble_store_value_sec) == 88 &&
              sizeof(ble_store_value_cccd) == 16 && sizeof(ble_store_value_rpa_rec) == 14 &&
              sizeof(ble_store_value_csfc) == 8 && sizeof(ble_store_value_local_irk) == 23,
              "NimBLE persisted ABI changed");

namespace {
constexpr char Tag[] = "ble_store";
constexpr char NewNamespace[] = "yumo_bonds";
constexpr char OldNamespace[] = "nimble_bond";
constexpr size_t HeaderSize = 16;
// Also retain unusual legacy CCCDs belonging to more than 16 identities.
constexpr size_t MaxBlobSize = HeaderSize + 128 * (7 + 6);
enum class Kind : uint8_t { Our = 1, Peer, Cccd, Rpa, Csfc };

// NimBLE calls persistence while holding its host lock; initialization runs
// before the host. Shared scratch buffers keep these calls off the host stack.
ble_store_config_vars_t committed{};
uint8_t encoded[MaxBlobSize]{};
uint8_t verified[MaxBlobSize]{};
std::atomic<esp_err_t> status{ESP_ERR_INVALID_STATE};

struct Slot {
    Kind kind;
    const char *key;
    const char *legacyPrefix;
    void *items;
    int *count;
    void *savedItems;
    int *savedCount;
    uint16_t *counter;
    uint16_t *savedCounter;
    size_t itemSize;
    int capacity;
};

Slot slot(Kind kind) {
    auto *v = ble_store_config_vars;
    switch (kind) {
    case Kind::Our:
        return {kind, "our_v1", "our_sec", v->_ble_store_config_our_secs,
            &v->_ble_store_config_num_our_secs, committed._ble_store_config_our_secs,
            &committed._ble_store_config_num_our_secs, &v->_ble_store_config_our_bond_count,
            &committed._ble_store_config_our_bond_count, sizeof(ble_store_value_sec), 16};
    case Kind::Peer:
        return {kind, "peer_v1", "peer_sec", v->_ble_store_config_peer_secs,
            &v->_ble_store_config_num_peer_secs, committed._ble_store_config_peer_secs,
            &committed._ble_store_config_num_peer_secs, &v->_ble_store_config_peer_bond_count,
            &committed._ble_store_config_peer_bond_count, sizeof(ble_store_value_sec), 16};
    case Kind::Cccd:
        return {kind, "cccd_v1", "cccd_sec", v->_ble_store_config_cccds,
            &v->_ble_store_config_num_cccds, committed._ble_store_config_cccds,
            &committed._ble_store_config_num_cccds, nullptr, nullptr,
            sizeof(ble_store_value_cccd), 128};
    case Kind::Rpa:
        return {kind, "rpa_v1", "rpa_rec", v->_ble_store_config_rpa_recs,
            &v->_ble_store_config_num_rpa_recs, committed._ble_store_config_rpa_recs,
            &committed._ble_store_config_num_rpa_recs, nullptr, nullptr,
            sizeof(ble_store_value_rpa_rec), 16};
    default:
        return {kind, "csfc_v1", "csfc_sec", v->_ble_store_config_csfcs,
            &v->_ble_store_config_num_csfcs, committed._ble_store_config_csfcs,
            &committed._ble_store_config_num_csfcs, nullptr, nullptr,
            sizeof(ble_store_value_csfc), MYNEWT_VAL(BLE_STORE_MAX_CSFCS)};
    }
}

uint16_t get16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
uint32_t get32(const uint8_t *p) { return get16(p) | uint32_t(get16(p + 2)) << 16; }
void put32(uint8_t *p, uint32_t v) { put16(p, uint16_t(v)); put16(p + 2, uint16_t(v >> 16)); }
uint32_t crcBytes(uint32_t crc, const uint8_t *p, size_t n) {
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i != 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
uint32_t crc(const uint8_t *p, size_t n) {
    return ~crcBytes(crcBytes(0xffffffffu, p, 12), p + HeaderSize, n - HeaderSize);
}
bool addressValid(const ble_addr_t &a) { return a.type <= BLE_ADDR_RANDOM_ID; }

bool itemsValid(const Slot &s) {
    if (*s.count < 0 || *s.count > s.capacity) return false;
    for (int i = 0; i < *s.count; ++i) {
        auto p = static_cast<const uint8_t *>(s.items) + i * s.itemSize;
        // Every supported object starts with an address, without padding.
        if (!addressValid(*reinterpret_cast<const ble_addr_t *>(p))) return false;
        if (s.kind == Kind::Our || s.kind == Kind::Peer) {
            if (reinterpret_cast<const ble_store_value_sec *>(p)->key_size > 16) return false;
        } else if (s.kind == Kind::Rpa) {
            if (!addressValid(reinterpret_cast<const ble_store_value_rpa_rec *>(p)->peer_addr)) return false;
        }
    }
    return true;
}

size_t encode(const Slot &s) {
    if (!itemsValid(s)) return 0;
    std::memset(encoded, 0, HeaderSize);
    std::memcpy(encoded, "YBS1", 4);
    encoded[4] = static_cast<uint8_t>(s.kind);
    put16(encoded + 6, uint16_t(s.itemSize));
    put16(encoded + 8, uint16_t(*s.count));
    size_t length = HeaderSize;
    if (s.kind == Kind::Cccd) {
        auto *items = static_cast<const ble_store_value_cccd *>(s.items);
        uint8_t indices[128]{};
        unsigned peers = 0;
        for (int i = 0; i < *s.count; ++i) {
            unsigned peer = 0;
            for (; peer < peers; ++peer)
                if (!std::memcmp(encoded + HeaderSize + peer * 7, &items[i].peer_addr, 7)) break;
            if (peer == peers) {
                std::memcpy(encoded + HeaderSize + peers * 7, &items[i].peer_addr, 7);
                ++peers;
            }
            indices[i] = uint8_t(peer);
        }
        put16(encoded + 10, uint16_t(peers));
        length += peers * 7;
        for (int i = 0; i < *s.count; ++i) {
            auto *p = encoded + length;
            p[0] = indices[i];
            put16(p + 1, items[i].chr_val_handle);
            put16(p + 3, items[i].flags);
            p[5] = items[i].value_changed;
            length += 6;
        }
    } else {
        length += size_t(*s.count) * s.itemSize;
        if (length > sizeof(encoded)) return 0;
        // Security, including LTK/IRK/CSRK, identity, flags and counters, remains
        // byte-for-byte identical to the existing 88-byte SDK objects.
        std::memcpy(encoded + HeaderSize, s.items, length - HeaderSize);
    }
    put32(encoded + 12, crc(encoded, length));
    return length;
}

esp_err_t decode(const Slot &s, const uint8_t *p, size_t n) {
    if (n < HeaderSize || std::memcmp(p, "YBS1", 4) || p[4] != uint8_t(s.kind) ||
        p[5] || get16(p + 6) != s.itemSize || get16(p + 8) > s.capacity ||
        get32(p + 12) != crc(p, n)) return ESP_ERR_INVALID_CRC;
    const unsigned count = get16(p + 8), peers = get16(p + 10);
    if (s.kind == Kind::Cccd) {
        if (peers > count || n != HeaderSize + peers * 7 + count * 6) return ESP_ERR_INVALID_SIZE;
        auto *items = static_cast<ble_store_value_cccd *>(s.items);
        std::memset(items, 0, s.capacity * s.itemSize);
        for (unsigned i = 0; i < count; ++i) {
            const auto *entry = p + HeaderSize + peers * 7 + i * 6;
            if (entry[0] >= peers || entry[5] > 1) return ESP_ERR_INVALID_CRC;
            std::memcpy(&items[i].peer_addr, p + HeaderSize + entry[0] * 7, 7);
            items[i].chr_val_handle = get16(entry + 1);
            items[i].flags = get16(entry + 3);
            items[i].value_changed = entry[5];
        }
    } else {
        if (peers || n != HeaderSize + count * s.itemSize) return ESP_ERR_INVALID_SIZE;
        std::memcpy(s.items, p + HeaderSize, count * s.itemSize);
    }
    *s.count = int(count);
    return itemsValid(s) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t readBlob(nvs_handle_t h, const char *key, void *dst, size_t capacity, size_t &size) {
    size = 0;
    auto err = nvs_get_blob(h, key, nullptr, &size);
    if (err != ESP_OK) return err;
    if (size > capacity) return ESP_ERR_INVALID_SIZE;
    return nvs_get_blob(h, key, dst, &size);
}

esp_err_t loadLegacy(nvs_handle_t oldHandle, const Slot &s) {
    *s.count = 0;
    if (!oldHandle) return ESP_OK;
    for (int index = 1; index <= s.capacity; ++index) {
        char key[16];
        std::snprintf(key, sizeof(key), "%s_%d", s.legacyPrefix, index);
        size_t size = 0;
        auto err = readBlob(oldHandle, key, static_cast<uint8_t *>(s.items) + *s.count * s.itemSize,
                            s.itemSize, size);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK) return err;
        if (size != s.itemSize) return ESP_ERR_INVALID_SIZE;
        ++*s.count;
    }
    return itemsValid(s) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

// A complete new blob is the authority, even when empty. Never erase it to
// represent deletion: otherwise old keys left by an interrupted cleanup could
// resurrect a deleted bond or subscription at the next boot.
esp_err_t writeVerified(nvs_handle_t h, const Slot &s) {
    const size_t size = encode(s);
    if (!size) return ESP_ERR_INVALID_SIZE;
    auto err = nvs_set_blob(h, s.key, encoded, size);
    if (err == ESP_OK) err = nvs_commit(h);
    size_t readSize = 0;
    auto readErr = readBlob(h, s.key, verified, sizeof(verified), readSize);
    // NVS writes are immediate. A reported failure after the last flash write
    // can still mean a durable success; only a full readback proves it.
    if (readErr == ESP_OK && readSize == size && !std::memcmp(encoded, verified, size)) return ESP_OK;
    return err != ESP_OK ? err : readErr != ESP_OK ? readErr : ESP_ERR_INVALID_CRC;
}

esp_err_t cleanup(nvs_handle_t oldHandle, const Slot &s) {
    if (!oldHandle) return ESP_OK;
    for (int index = 1; index <= s.capacity; ++index) {
        char key[16];
        std::snprintf(key, sizeof(key), "%s_%d", s.legacyPrefix, index);
        auto err = nvs_erase_key(oldHandle, key);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    }
    return nvs_commit(oldHandle);
}

void saveCommitted(const Slot &s) {
    std::memcpy(s.savedItems, s.items, s.capacity * s.itemSize);
    *s.savedCount = *s.count;
    if (s.counter) *s.savedCounter = *s.counter;
}
void restoreCommitted(const Slot &s) {
    std::memcpy(s.items, s.savedItems, s.capacity * s.itemSize);
    *s.count = *s.savedCount;
    if (s.counter) *s.counter = *s.savedCounter;
}

int persist(Kind kind) {
    if (!ble_store_config_vars) return BLE_HS_ESTORE_FAIL;
    const auto s = slot(kind);
    if (status.load() != ESP_OK) {
        // SDK callers have already changed RAM even when a previous operation
        // latched a failure. Roll every subsequent kind back as well.
        restoreCommitted(s);
        return BLE_HS_ESTORE_FAIL;
    }
    nvs_handle_t h = 0;
    auto err = nvs_open(NewNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = writeVerified(h, s);
        nvs_close(h);
    }
    if (err == ESP_OK) {
        saveCommitted(s);
        return 0;
    }
    // Stock SDK mutates arrays before calling persistence. Keep RAM consistent
    // with the last known durable state, and refuse further writes until init
    // has reconciled any ambiguous flash failure. Never invoke bond eviction.
    restoreCommitted(s);
    status = err;
    ESP_LOGE(Tag, "bond persistence failed (kind=%u, rc=%d); reboot device to recover", unsigned(kind), int(err));
    return BLE_HS_ESTORE_FAIL;
}

esp_err_t initialize() {
    if (!ble_store_config_vars) return ESP_ERR_INVALID_STATE;
    // Clearing RAM here does not touch any persisted namespace or device key.
    std::memset(ble_store_config_vars, 0, sizeof(*ble_store_config_vars));
    std::memset(&committed, 0, sizeof(committed));
    nvs_handle_t h = 0, oldHandle = 0;
    auto err = nvs_open(NewNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_open(OldNamespace, NVS_READWRITE, &oldHandle);
    if (err != ESP_OK) { nvs_close(h); return err; }

    // A durable authority bit distinguishes first boot from a missing new blob.
    // Without it, damage to a previously migrated blob could look like a fresh
    // empty store after its equivalent legacy keys were removed.
    uint8_t authorityMask = 0;
    err = nvs_get_u8(h, "authority", &authorityMask);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK && (authorityMask & ~uint8_t(31))) err = ESP_ERR_INVALID_CRC;

    constexpr Kind order[] = {Kind::Cccd, Kind::Rpa, Kind::Csfc, Kind::Our, Kind::Peer};
    bool authoritative[5]{};
    // Read and validate every object before the first migration write. An
    // unreadable new format never falls back to stale legacy security keys.
    for (unsigned i = 0; i < 5 && err == ESP_OK; ++i) {
        auto s = slot(order[i]);
        size_t size = 0;
        err = readBlob(h, s.key, verified, sizeof(verified), size);
        if (err == ESP_ERR_NVS_NOT_FOUND && !(authorityMask & (1u << (unsigned(s.kind) - 1))))
            err = loadLegacy(oldHandle, s);
        else if (err == ESP_OK) { authoritative[i] = true; err = decode(s, verified, size); }
    }
    // Local identity stays in the original SDK namespace and binary format.
    if (err == ESP_OK) {
        for (int index = 1; index <= 16; ++index) {
            char key[16];
            std::snprintf(key, sizeof(key), "local_irk_%d", index);
            ble_store_value_local_irk value{};
            size_t size = 0;
            err = readBlob(oldHandle, key, &value, sizeof(value), size);
            if (err == ESP_ERR_NVS_NOT_FOUND) { err = ESP_OK; continue; }
            if (err != ESP_OK) break;
            if (size != sizeof(value) || !addressValid(value.addr) || ble_store_config_num_local_irks) {
                err = ESP_ERR_INVALID_SIZE; break;
            }
            ble_store_config_local_irks[ble_store_config_num_local_irks++] = value;
        }
    }
    for (const Kind kind : {Kind::Our, Kind::Peer}) {
        auto s = slot(kind);
        auto *secs = static_cast<ble_store_value_sec *>(s.items);
        // Match stock restoration's bond ordering without changing key values.
        std::sort(secs, secs + *s.count, [](const auto &a, const auto &b) { return a.bond_count < b.bond_count; });
        *s.counter = *s.count ? secs[*s.count - 1].bond_count : 0;
    }
    for (unsigned i = 0; i < 5 && err == ESP_OK; ++i) {
        auto s = slot(order[i]);
        if (!authoritative[i]) err = writeVerified(h, s);
        if (err != ESP_OK) break;
        const uint8_t nextMask = authorityMask | uint8_t(1u << (unsigned(s.kind) - 1));
        if (nextMask != authorityMask) {
            err = nvs_set_u8(h, "authority", nextMask);
            if (err == ESP_OK) err = nvs_commit(h);
            uint8_t readMask = 0;
            const auto readErr = nvs_get_u8(h, "authority", &readMask);
            if (readErr == ESP_OK && readMask == nextMask) err = ESP_OK;
            else if (err == ESP_OK) err = readErr != ESP_OK ? readErr : ESP_ERR_INVALID_CRC;
            if (err != ESP_OK) break;
            authorityMask = nextMask;
        }
        // Only remove equivalent old fields after the new object is durable.
        // Cleanup failure is recoverable: this new key remains authoritative.
        auto cleanErr = cleanup(oldHandle, s);
        if (cleanErr != ESP_OK) ESP_LOGW(Tag, "legacy cleanup deferred (kind=%u, rc=%d)", unsigned(s.kind), int(cleanErr));
        saveCommitted(s);
    }
    nvs_close(oldHandle);
    nvs_close(h);
    return err;
}
} // namespace

extern "C" esp_err_t yumo_ble_store_status(void) { return status.load(); }
extern "C" void __wrap_ble_store_config_conf_init(void) {
    status = ESP_ERR_INVALID_STATE;
    status = initialize();
    if (status.load() != ESP_OK) ESP_LOGE(Tag, "bond restore/migration failed rc=%d; existing data retained", int(status.load()));
}
extern "C" int __wrap_ble_store_config_persist_our_secs(void) { return persist(Kind::Our); }
extern "C" int __wrap_ble_store_config_persist_peer_secs(void) { return persist(Kind::Peer); }
extern "C" int __wrap_ble_store_config_persist_cccds(void) { return persist(Kind::Cccd); }
extern "C" int __wrap_ble_store_config_persist_rpa_recs(void) { return persist(Kind::Rpa); }
extern "C" int __wrap_ble_store_config_persist_csfcs(void) { return persist(Kind::Csfc); }
