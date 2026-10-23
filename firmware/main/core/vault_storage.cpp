#include "vault_storage.hpp"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <array>
#include <cstring>
namespace vault {
namespace {
constexpr size_t SlotSize = 0x80000, Sector = 4096, ConfigMax = 8192;
// The vault worker/bundles live in PSRAM. Use one bounded internal buffer for
// ALL flash transfers, including headers; the driver otherwise allocates a
// second internal bounce buffer for every read. No persistent RAM reservation.
class FlashIo {
    static constexpr size_t Chunk = 1024;
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(Chunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::atomic<bool> &cancel;
public:
    explicit FlashIo(const std::atomic<bool> &c) : cancel(c) {}
    ~FlashIo() { if(buffer) { wipe(buffer, Chunk); heap_caps_free(buffer); } }
    explicit operator bool() const { return buffer != nullptr; }
    esp_err_t transfer(const esp_partition_t *p, size_t offset, void *data, size_t size, bool writing) {
        if(!buffer) return ESP_ERR_NO_MEM;
        auto bytes = static_cast<uint8_t *>(data);
        for(size_t done=0;done<size;) {
            if(cancel.load()) return ESP_ERR_INVALID_STATE;
            const size_t n=std::min(Chunk,size-done);
            if(writing) std::memcpy(buffer,bytes+done,n);
            const auto rc=writing?esp_partition_write(p,offset+done,buffer,n):esp_partition_read(p,offset+done,buffer,n);
            if(rc!=ESP_OK) {
                // Failure only; never include stored bytes, PINs or key material.
                ESP_LOGW("vault", "Partition %s failed: %s",writing?"write":"read",esp_err_to_name(rc));
                return rc;
            }
            if(!writing) std::memcpy(bytes+done,buffer,n);
            done+=n;
            if((done&4095)==0) vTaskDelay(1);
        }
        return ESP_OK;
    }
    esp_err_t read(const esp_partition_t *p,size_t off,void *out,size_t n) {return transfer(p,off,out,n,false);}
    esp_err_t write(const esp_partition_t *p,size_t off,const void *in,size_t n) {return transfer(p,off,const_cast<void *>(in),n,true);}
};
const esp_partition_t *partition() {
    auto p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      static_cast<esp_partition_subtype_t>(0x40), "vault");
    return p && p->address == 0xEC0000 && p->size == 0x100000 ? p : nullptr;
}
enum class SlotResult { Valid, Invalid, ReadError, VerifyError };
SlotResult slot(FlashIo &io, const esp_partition_t *p, int index, Disk &out,
                const std::atomic<bool> &cancel, const char **failureStage = nullptr) {
    auto stage = [failureStage](const char *name) {
        if (failureStage) *failureStage = name;
    };
    std::array<uint8_t, 96> h{};
    uint8_t digest[32];
    size_t offset = index * SlotSize;
    stage("cancelled");
    if (cancel.load())
        return SlotResult::Invalid;
    stage("header-read");
    if (io.read(p, offset, h.data(), h.size()) != ESP_OK)
        return SlotResult::ReadError;
    stage("header-magic");
    if (std::memcmp(h.data(), "JVSLOT1", 8))
        return SlotResult::Invalid;
    uint32_t generation = read32(h.data() + 8), b = read32(h.data() + 12),
             c = read32(h.data() + 16);
    stage("header-fields");
    if (!generation || b < 144 || b > MaxBundle || c < 28 || c > ConfigMax ||
        nonzero(h.data() + 20, 12))
        return SlotResult::Invalid;
    stage("header-sha-operation");
    if (!hash(h.data(), 64, digest))
        return SlotResult::VerifyError;
    stage("header-digest");
    if (!equal(digest, h.data() + 64, 32))
        return SlotResult::Invalid;
    Bytes data(b + c);
    stage("payload-read");
    if (io.read(p, offset + Sector, data.data(), data.size()) != ESP_OK)
        return SlotResult::ReadError;
    stage("cancelled");
    if (cancel.load())
        return SlotResult::Invalid;
    stage("payload-sha-operation");
    if (!hash(data.data(), data.size(), digest))
        return SlotResult::VerifyError;
    stage("payload-digest");
    if (!equal(digest, h.data() + 32, 32))
        return SlotResult::Invalid;
    stage("bundle-framing");
    if (!framing(data.data(), b))
        return SlotResult::Invalid;
    out.bundle.assign(data.begin(), data.begin() + b);
    out.config.assign(data.begin() + b, data.end());
    out.slot = index;
    out.generation = generation;
    stage("valid");
    return SlotResult::Valid;
}
// Failure-only known-answer checks. The short case uses the header path; the
// 1KiB case also exercises multi-block hashing. This is
// diagnostic evidence, never a substitute for checking either stored digest.
bool hashSelfTest() {
    static constexpr uint8_t expected[2][32] = {
        {0xf5,0xa5,0xfd,0x42,0xd1,0x6a,0x20,0x30,0x27,0x98,0xef,0x6e,0xd3,0x09,0x97,0x9b,
         0x43,0x00,0x3d,0x23,0x20,0xd9,0xf0,0xe8,0xea,0x98,0x31,0xa9,0x27,0x59,0xfb,0x4b},
        {0x5f,0x70,0xbf,0x18,0xa0,0x86,0x00,0x70,0x16,0xe9,0x48,0xb0,0x4a,0xed,0x3b,0x82,
         0x10,0x3a,0x36,0xbe,0xa4,0x17,0x55,0xb6,0xcd,0xdf,0xaf,0x10,0xac,0xe3,0xc6,0xef}
    };
    Bytes sample(1024, 0);
    uint8_t digest[32];
    bool passed = true;
    for (unsigned i = 0; i < 2; ++i) {
        const size_t size = i ? 1024 : 64;
        const bool completed = hash(sample.data(), size, digest);
        const bool matches = completed && equal(digest, expected[i], 32);
        // Never log stored bytes/digests, identity, PIN, or key material.
        ESP_LOGW("vault", "SHA self-test: bytes=%u completed=%d matches=%d",
                 static_cast<unsigned>(size), completed, matches);
        passed = passed && matches;
    }
    wipe(digest, sizeof(digest));
    return passed;
}
} // namespace
bool load(Disk &disk, std::string &error, const std::atomic<bool> &cancel) {
    const auto p = partition();
    if (!p) {
        error = "密码分区缺失，请先更新分区表";
        return false;
    }
    FlashIo io(cancel);
    if(!io) { error="密码区读取内存不足，请稍后重试"; return false; }
    Disk a, b;
    const char *as = "pending", *bs = "pending";
    const auto ar = slot(io, p, 0, a, cancel, &as), br = slot(io, p, 1, b, cancel, &bs);
    if (cancel.load()) {
        error = "已取消";
        return false;
    }
    // An unreadable slot may be the newest one. Do not call it corrupt/blank
    // or overwrite it after falling back to an older generation.
    if (ar == SlotResult::ReadError || br == SlotResult::ReadError) {
        ESP_LOGW("vault", "Load I/O failed: slot0=%s slot1=%s", as, bs);
        error = "密码分区读取失败，请重试";
        return false;
    }
    // A failed crypto operation says nothing about the stored data. Do not
    // label it corrupt/blank or choose an older slot when verification failed.
    if (ar == SlotResult::VerifyError || br == SlotResult::VerifyError) {
        ESP_LOGW("vault", "Load SHA failed: slot0=%s slot1=%s", as, bs);
        error = "密码区校验未完成，请重试";
        return false;
    }
    const bool av = ar == SlotResult::Valid, bv = br == SlotResult::Valid;
    if (av || bv) {
        disk = std::move(bv && (!av || b.generation > a.generation) ? b : a);
        return true;
    }
    std::array<uint8_t, Sector> scratch{};
    disk.blank = true;
    for (size_t off = 0; off < p->size; off += Sector) {
        if (cancel.load()) {
            error = "已取消";
            return false;
        }
        if (io.read(p, off, scratch.data(), scratch.size()) != ESP_OK) {
            error = "密码分区读取失败";
            return false;
        }
        for (auto x : scratch)
            if (x != 0xff) {
                disk.blank = false;
                // A nonblank, unverified partition is NOT a successful load.
                // Preserve it and publish the actual failure instead of Ready
                // with exists=false/blank=false and a misleading corrupt label.
                ESP_LOGW("vault", "Load rejected: slot0=%s slot1=%s", as, bs);
                const bool hashOk = hashSelfTest();
                if (cancel.load()) error = "已取消";
                else error = hashOk ? "密码区校验失败，原数据已保留。请重新读取。"
                                    : "密码区校验组件异常，原数据已保留";
                return false;
            }
        if ((off & 0xffff) == 0)
            vTaskDelay(1);
    }
    return true;
}
bool commit(const Disk &previous, const Bytes &bundle, const Bytes &config, std::string &error,
            const std::atomic<bool> &cancel) {
    const auto p = partition();
    if (!p || (!previous.blank && previous.slot < 0) || !framing(bundle.data(), bundle.size()) ||
        config.size() < 28 || config.size() > ConfigMax || previous.generation == UINT32_MAX) {
        error = "分区未初始化或文件无效";
        return false;
    }
    if (cancel.load()) {
        error = "已取消";
        return false;
    }
    const int target = previous.slot == 0 ? 1 : 0;
    FlashIo io(cancel);
    if(!io) { error="密码区保存内存不足，原数据保留"; return false; }
    size_t offset = target * SlotSize;
    // Inactive slot only. Header is committed LAST, after data readback/hash.
    for (size_t i = 0; i < SlotSize; i += Sector) {
        if (cancel.load()) {
            error = "已取消，原版本保留";
            return false;
        }
        if (esp_partition_erase_range(p, offset + i, Sector) != ESP_OK) {
            error = "密码分区擦除失败";
            return false;
        }
        vTaskDelay(1);
    }
    Bytes data = bundle;
    data.insert(data.end(), config.begin(), config.end());
    for (size_t i = 0; i < data.size(); i += Sector) {
        if (cancel.load()) {
            error = "已取消，原版本保留";
            return false;
        }
        if (io.write(p, offset + Sector + i, data.data() + i,
                                std::min(Sector, data.size() - i)) != ESP_OK) {
            error = "密码分区写入失败";
            return false;
        }
        vTaskDelay(1);
    }
    uint8_t digest[32], verified[32];
    Bytes check(data.size());
    if (!hash(data.data(), data.size(), digest) ||
        io.read(p, offset + Sector, check.data(), check.size()) != ESP_OK ||
        !hash(check.data(), check.size(), verified) || !equal(digest, verified, 32) ||
        cancel.load()) {
        error = "写入校验失败或已取消";
        return false;
    }
    std::array<uint8_t, 96> h{};
    std::memcpy(h.data(), "JVSLOT1", 8);
    write32(h.data() + 8, previous.generation + 1);
    write32(h.data() + 12, bundle.size());
    write32(h.data() + 16, config.size());
    std::memcpy(h.data() + 32, digest, 32);
    if (!hash(h.data(), 64, h.data() + 64) ||
        io.write(p, offset, h.data(), h.size()) != ESP_OK) {
        error = "密码分区提交失败";
        return false;
    }
    Disk result;
    if (slot(io, p, target, result, cancel) != SlotResult::Valid ||
        result.generation != previous.generation + 1) {
        error = "提交后验证未完成，请重新打开";
        return false;
    }
    return true;
}
} // namespace vault
