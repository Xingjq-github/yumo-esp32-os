// Production application removal + real pinned SDK C unpair/store utilities.
// Only the store, metadata and controller boundary are synthetic; no real keys.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "sdk_bond_stubs.h"
#include "core/ble_hid.hpp"
#include "core/ble_peer_store.hpp"

unsigned scenarios = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "scenario %u line %d: %s\n", scenarios, __LINE__, #x); std::exit(1); } } while (0)
template<class... T> void log(T...) {}
#define ESP_LOGW(...) log(__VA_ARGS__)
#define ESP_LOGE(...) log(__VA_ARGS__)
const char *esp_err_to_name(int) { return "synthetic error"; }

namespace fake {
struct Record { int type; ble_store_value value; };
std::vector<Record> records;
std::array<bool, 16> metadata{};
std::array<sys::BleBond, 16> peers{};
sys::BleBond target{}, active{};
ble_hs_conn connection{};
bool hostEnabled = true, advertising = false, scanning = false;
int storeStatus = ESP_OK, enumerateError = 0, verifyOurError = 0, verifyPeerError = 0;
int deleteFailureAt = -1, failureStatus = ESP_ERR_FLASH_OP_FAIL, absentType = 0, privacyError = 0;
unsigned attempts = 0, deleted = 0, forgotten = 0, iterations = 0, readOur = 0, readPeer = 0;
unsigned terminated = 0, cancelled = 0, closed = 0, stopped = 0;
int hostLock = 0;
ble_addr_t address(const sys::BleBond &b) { ble_addr_t a{}; a.type = b.addrType; std::memcpy(a.val, b.addr, 6); return a; }
bool equal(const ble_addr_t &a, const ble_addr_t &b) { return ble_addr_cmp(&a, &b) == 0; }
ble_addr_t keyAddress(int type, const ble_store_key *key) {
    if (type == BLE_STORE_OBJ_TYPE_PEER_ADDR) return key->rpa_rec.peer_rpa_addr;
    if (type == BLE_STORE_OBJ_TYPE_CCCD) return key->cccd.peer_addr;
    if (type == BLE_STORE_OBJ_TYPE_CSFC) return key->csfc.peer_addr;
    return key->sec.peer_addr;
}
ble_addr_t valueAddress(const Record &record) {
    if (record.type == BLE_STORE_OBJ_TYPE_PEER_ADDR) return record.value.rpa_rec.peer_rpa_addr;
    if (record.type == BLE_STORE_OBJ_TYPE_CCCD) return record.value.cccd.peer_addr;
    if (record.type == BLE_STORE_OBJ_TYPE_CSFC) return record.value.csfc.peer_addr;
    return record.value.sec.peer_addr;
}
bool has(int type, const ble_addr_t &a) {
    return std::any_of(records.begin(), records.end(), [&](const auto &r) { return r.type == type && equal(valueAddress(r), a); });
}
bool hasAny(unsigned index) {
    const auto a = address(peers[index]);
    return std::any_of(records.begin(), records.end(), [&](const auto &r) { return equal(valueAddress(r), a); });
}
void add(unsigned index, bool our = true, bool peer = true) {
    const auto a = address(peers[index]);
    auto addSec = [&](int type) { Record r{}; r.type = type; r.value.sec.peer_addr = a; r.value.sec.irk_present = 1; records.push_back(r); };
    if (our) addSec(BLE_STORE_OBJ_TYPE_OUR_SEC);
    if (peer) addSec(BLE_STORE_OBJ_TYPE_PEER_SEC);
    for (unsigned i = 0; i < 8; ++i) { Record r{}; r.type = BLE_STORE_OBJ_TYPE_CCCD; r.value.cccd.peer_addr = a; r.value.cccd.chr_val_handle = uint16_t(i + 1); records.push_back(r); }
    Record rpa{}; rpa.type = BLE_STORE_OBJ_TYPE_PEER_ADDR; rpa.value.rpa_rec.peer_rpa_addr = rpa.value.rpa_rec.peer_addr = a; records.push_back(rpa);
    Record csfc{}; csfc.type = BLE_STORE_OBJ_TYPE_CSFC; csfc.value.csfc.peer_addr = a; records.push_back(csfc);
    metadata[index] = true;
}
}

extern "C" {
int ble_hs_is_enabled() { return fake::hostEnabled; }
int ble_gap_adv_active() { return fake::advertising; }
int ble_gap_disc_active() { return fake::scanning; }
void ble_hs_lock() { CHECK(fake::hostLock++ == 0); }
void ble_hs_unlock() { CHECK(--fake::hostLock == 0); }
ble_hs_conn *ble_hs_conn_find_by_addr(const ble_addr_t *a) {
    CHECK(fake::hostLock == 1);
    return fake::active.addr[0] && fake::equal(*a, fake::address(fake::active)) ? &fake::connection : nullptr;
}
int ble_gap_terminate_with_conn(ble_hs_conn *conn, uint8_t reason) {
    CHECK(conn == &fake::connection && reason == BLE_ERR_REM_USER_CONN_TERM); ++fake::terminated; return 0;
}
int ble_hs_pvcy_remove_entry(uint8_t, const uint8_t *) { return fake::privacyError; }
int ble_gap_conn_cancel() { ++fake::cancelled; return 0; }
int ble_store_read(int type, const ble_store_key *key, ble_store_value *value) {
    const auto a = fake::keyAddress(type, key);
    for (const auto &r : fake::records) if (r.type == type && fake::equal(fake::valueAddress(r), a)) { *value = r.value; return 0; }
    return BLE_HS_ENOENT;
}
int ble_store_read_our_sec(const ble_store_key_sec *key, ble_store_value_sec *value) {
    ++fake::readOur; if (fake::verifyOurError) return fake::verifyOurError;
    ble_store_key k{}; k.sec = *key; ble_store_value v{}; const int rc = ble_store_read(BLE_STORE_OBJ_TYPE_OUR_SEC, &k, &v); *value = v.sec; return rc;
}
int ble_store_read_peer_sec(const ble_store_key_sec *key, ble_store_value_sec *value) {
    ++fake::readPeer; if (fake::verifyPeerError) return fake::verifyPeerError;
    ble_store_key k{}; k.sec = *key; ble_store_value v{}; const int rc = ble_store_read(BLE_STORE_OBJ_TYPE_PEER_SEC, &k, &v); *value = v.sec; return rc;
}
int ble_store_delete(int type, const ble_store_key *key) {
    ++fake::attempts;
    const auto a = fake::keyAddress(type, key);
    const auto it = std::find_if(fake::records.begin(), fake::records.end(), [&](const auto &r) { return r.type == type && fake::equal(fake::valueAddress(r), a); });
    if (it == fake::records.end() || type == fake::absentType) return BLE_HS_ENOENT;
    if (fake::storeStatus != ESP_OK) return BLE_HS_ESTORE_FAIL;
    if (fake::deleteFailureAt >= 0 && fake::deleted == unsigned(fake::deleteFailureAt)) {
        fake::storeStatus = fake::failureStatus; return BLE_HS_ESTORE_FAIL;
    }
    fake::records.erase(it); ++fake::deleted; return 0;
}
int ble_store_iterate(int type, ble_store_iterator_fn *callback, void *arg) {
    ++fake::iterations;
    if (fake::enumerateError) return fake::enumerateError;
    CHECK(type == BLE_STORE_OBJ_TYPE_OUR_SEC);
    for (auto &r : fake::records) if (r.type == type) { const auto rc = callback(type, &r.value, arg); if (rc) return rc; }
    return 0;
}
}
esp_err_t yumo_ble_store_status() { return fake::storeStatus; }

namespace sys {
constexpr int kMaxBondSlots = BlePeerStore::Capacity;
constexpr uint16_t kInvalidHandle = 0xffff;
constexpr const char *kTag = "bond-removal";
std::atomic<bool> s_initialized{true}, s_enabled{true}, s_connectPending{false}, s_bondStoreFull{true};
std::atomic<int> s_lastError{0};
std::atomic<uint16_t> s_connHandle{kInvalidHandle};
std::atomic<BleHidState> s_state{BleHidState::Ready};
void endConnectionWindow() { ++fake::closed; }
void BleHid::stopScan() { ++fake::stopped; }
bool BlePeerStore::same(const BleBond &a, const BleBond &b) { return (a.addrType & 1) == (b.addrType & 1) && !std::memcmp(a.addr, b.addr, 6); }
BleBond BlePeerStore::target() { return fake::target; }
BleBond BlePeerStore::active() { return fake::active; }
void BlePeerStore::forget(const BleBond &bond) {
    const auto a = fake::address(bond);
    CHECK(!fake::has(BLE_STORE_OBJ_TYPE_OUR_SEC, a));
    CHECK(!fake::has(BLE_STORE_OBJ_TYPE_PEER_SEC, a));
    CHECK(fake::storeStatus == ESP_OK);
    for (unsigned i = 0; i < fake::peers.size(); ++i) if (same(bond, fake::peers[i])) fake::metadata[i] = false;
    if (same(fake::target, bond)) fake::target = {};
    ++fake::forgotten;
}
#include "bond_removal.inc"
}

void reset(unsigned count = 1) {
    CHECK(fake::hostLock == 0); ++scenarios;
    fake::records.clear(); fake::metadata.fill(false); fake::target = {}; fake::active = {};
    fake::hostEnabled = true; fake::advertising = fake::scanning = false;
    fake::storeStatus = fake::enumerateError = fake::verifyOurError = fake::verifyPeerError = 0;
    fake::deleteFailureAt = -1; fake::failureStatus = ESP_ERR_FLASH_OP_FAIL; fake::absentType = fake::privacyError = 0;
    fake::attempts = fake::deleted = fake::forgotten = fake::iterations = fake::readOur = fake::readPeer = 0;
    fake::terminated = fake::cancelled = fake::closed = fake::stopped = 0;
    sys::s_initialized = sys::s_enabled = true; sys::s_connectPending = false; sys::s_bondStoreFull = true;
    sys::s_connHandle = sys::kInvalidHandle; sys::s_state = sys::BleHidState::Ready; sys::s_lastError = 0;
    for (unsigned i = 0; i < 16; ++i) {
        auto &p = fake::peers[i]; p = {}; p.addrType = uint8_t(i % 2); p.addr[0] = uint8_t(i + 1); p.addr[5] = 0xc0;
        std::snprintf(p.name, sizeof(p.name), "Synthetic mode %u", i); p.keyboard = i % 3 != 0;
        if (i < count) fake::add(i);
    }
}

int main() {
    using sys::BleHid;
    // Demonstrate the actual SDK flaw, without mocking unpair's return value.
    for (bool ourOnly : {false, true}) {
        reset(0); fake::add(0, true, !ourOnly); fake::deleteFailureAt = 0;
        auto a = fake::address(fake::peers[0]);
        CHECK(ble_gap_unpair(&a) == 0);
        CHECK(fake::storeStatus == ESP_ERR_FLASH_OP_FAIL && fake::hasAny(0));
    }

    reset(); CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK);
    CHECK(fake::records.empty() && !fake::metadata[0] && fake::forgotten == 1);
    CHECK(fake::readOur == 1 && fake::readPeer == 1 && !sys::s_bondStoreFull);
    const auto writes = fake::deleted;
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK); CHECK(fake::deleted == writes);

    for (bool all : {false, true}) {
        reset(16); sys::s_initialized = false;
        CHECK((all ? BleHid::removeAllBonds() : BleHid::removeBond(fake::peers[0])) == ESP_ERR_INVALID_STATE);
        CHECK(!fake::attempts && !fake::forgotten && fake::records.size() == 192);
        reset(16); fake::storeStatus = ESP_ERR_FLASH_OP_FAIL;
        CHECK((all ? BleHid::removeAllBonds() : BleHid::removeBond(fake::peers[0])) == ESP_ERR_FLASH_OP_FAIL);
        CHECK(!fake::attempts && !fake::iterations && !fake::forgotten && sys::s_bondStoreFull);
        CHECK(sys::s_lastError == ESP_ERR_FLASH_OP_FAIL);
    }

    // Each constituent delete can fail durably; retry stays failed until reboot.
    for (unsigned phase = 0; phase < 12; ++phase) {
        reset(16); fake::deleteFailureAt = int(phase);
        CHECK(BleHid::removeBond(fake::peers[0]) == ESP_ERR_FLASH_OP_FAIL);
        CHECK(fake::metadata[0] && !fake::forgotten && sys::s_bondStoreFull);
        CHECK(fake::hasAny(0) && fake::deleted == phase);
        const auto attempts = fake::attempts;
        CHECK(BleHid::removeBond(fake::peers[0]) == ESP_ERR_FLASH_OP_FAIL);
        CHECK(fake::attempts == attempts && fake::metadata[0]);
        // Simulated reconciled reboot: only fault latch is reset; durable state
        // includes successful earlier deletions, so fallback must finish it.
        fake::storeStatus = ESP_OK; fake::deleteFailureAt = -1;
        CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK);
        CHECK(!fake::hasAny(0) && !fake::metadata[0]);
        for (unsigned i = 1; i < 16; ++i) CHECK(fake::hasAny(i) && fake::metadata[i]);
    }
    reset(0); fake::add(0, true, false); fake::deleteFailureAt = 0;
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_ERR_FLASH_OP_FAIL && fake::metadata[0]);
    reset(0); fake::add(0, false, true); fake::deleteFailureAt = 0;
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_ERR_FLASH_OP_FAIL && fake::metadata[0]);

    // An API error need not latch storage status. Postconditions must still
    // catch either security direction remaining after apparent SDK success.
    for (int type : {BLE_STORE_OBJ_TYPE_OUR_SEC, BLE_STORE_OBJ_TYPE_PEER_SEC}) {
        reset(); fake::absentType = type;
        CHECK(BleHid::removeBond(fake::peers[0]) == ESP_FAIL);
        CHECK(fake::metadata[0] && !fake::forgotten && sys::s_bondStoreFull);
        CHECK(fake::has(type, fake::address(fake::peers[0])));
    }
    for (bool peerQuery : {false, true}) {
        reset(); (peerQuery ? fake::verifyPeerError : fake::verifyOurError) = BLE_HS_EUNKNOWN;
        CHECK(BleHid::removeBond(fake::peers[0]) == ESP_FAIL);
        CHECK(fake::records.empty() && fake::metadata[0] && !fake::forgotten);
        CHECK(sys::s_bondStoreFull && fake::readOur == 1 && fake::readPeer == 1);
    }
    reset(16); fake::enumerateError = BLE_HS_EUNKNOWN;
    CHECK(BleHid::removeAllBonds() == ESP_FAIL);
    CHECK(!fake::attempts && !fake::forgotten && sys::s_bondStoreFull);

    reset(16); CHECK(BleHid::removeAllBonds() == ESP_OK);
    CHECK(fake::records.empty() && fake::forgotten == 16 && !sys::s_bondStoreFull);
    CHECK(fake::readOur == 16 && fake::readPeer == 16);
    for (bool present : fake::metadata) CHECK(!present);
    reset(0); CHECK(BleHid::removeAllBonds() == ESP_OK && !fake::forgotten && !sys::s_bondStoreFull);

    // Cut the logical persistence path at every record among all 16 peers.
    for (unsigned failure = 0; failure < 192; ++failure) {
        reset(16); fake::deleteFailureAt = int(failure);
        CHECK(BleHid::removeAllBonds() == ESP_ERR_FLASH_OP_FAIL);
        CHECK(fake::forgotten == failure / 12 && fake::deleted == failure);
        for (unsigned i = 0; i < 16; ++i) {
            CHECK(fake::metadata[i] == (i >= failure / 12));
            CHECK(fake::hasAny(i) == (i >= failure / 12));
        }
    }

    // Different addresses/modes and same bytes in different address spaces
    // stay distinct; removing one never clears another device's metadata.
    for (unsigned index = 0; index < 16; ++index) {
        reset(16); CHECK(BleHid::removeBond(fake::peers[index]) == ESP_OK);
        for (unsigned i = 0; i < 16; ++i) CHECK(fake::metadata[i] == (i != index));
    }
    reset(0); fake::peers[1] = fake::peers[0]; fake::peers[1].addrType = 1;
    fake::add(0); fake::add(1);
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK);
    CHECK(fake::metadata[1] && fake::hasAny(1));

    for (int busy : {1, 2, 3}) {
        reset(); fake::advertising = busy == 1; fake::scanning = busy == 2; fake::hostEnabled = busy != 3;
        CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK);
        CHECK(fake::records.empty() && !fake::metadata[0]);
    }
    reset(2); fake::target = fake::peers[0]; fake::active = fake::peers[0]; sys::s_connectPending = true;
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK);
    CHECK(fake::closed == 1 && fake::stopped == 1 && fake::cancelled == 1 && fake::terminated == 1);
    CHECK(fake::metadata[1] && fake::hasAny(1));
    reset(2); fake::target = fake::peers[1]; fake::active = fake::peers[1];
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK);
    CHECK(!fake::closed && !fake::stopped && !fake::cancelled && !fake::terminated);
    CHECK(fake::metadata[1] && fake::hasAny(1));
    reset(); fake::privacyError = BLE_HS_EUNKNOWN;
    CHECK(BleHid::removeBond(fake::peers[0]) == ESP_OK && fake::records.empty());
    CHECK(fake::hostLock == 0);
    std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u}\n", scenarios, checks);
}
