#include "vault_crypto.hpp"
#include "vault_gcm.h"
#include "vault_sha256.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_mgr.hpp"
#include <algorithm>
#include <cstring>
namespace vault {
namespace {
// HMAC's keyed prefix states belong to this derive call only. Reusing the
// two completed pad blocks preserves PBKDF2 exactly without repeated PSA
// key-slot locking, heap allocations or shared SHA accelerator ownership.
struct HmacSha256 {
  vault_sha256_state inner{}, outer{}, work{};
  uint8_t pad[64]{}, digest[32]{};
  ~HmacSha256() { wipe(this, sizeof(*this)); }
  bool init(const char *password, size_t length) {
    // derive accepts only the existing 4..12 digit credentials (< block size).
    std::memset(pad, 0x36, sizeof(pad));
    for (size_t i = 0; i < length; ++i)
      pad[i] ^= static_cast<uint8_t>(password[i]);
    vault_sha256_init(&inner);
    if (vault_sha256_process(&inner, pad, sizeof(pad)) != 0)
      return false;
    for (auto &byte : pad)
      byte ^= 0x36 ^ 0x5c;
    vault_sha256_init(&outer);
    const bool ok = vault_sha256_process(&outer, pad, sizeof(pad)) == 0;
    wipe(pad, sizeof(pad));
    return ok;
  }
  bool compute(const uint8_t *input, size_t size, uint8_t output[32]) {
    work = inner;
    if (vault_sha256_process(&work, input, size) != 0 ||
        vault_sha256_done(&work, digest) != 0)
      return false;
    work = outer;
    return vault_sha256_process(&work, digest, sizeof(digest)) == 0 &&
           vault_sha256_done(&work, output) == 0;
  }
};
} // namespace
bool hash(const void *data, size_t size, uint8_t output[32]) {
  // Allocation-free, per-call software state also works on a PSRAM worker
  // stack. Stored SHA-256 bytes and the JVSLOT1 format remain unchanged.
  // No shared accelerator/PSA lock or temporary internal DMA allocations.
  vault_sha256_state state{};
  vault_sha256_init(&state);
  auto input = static_cast<const uint8_t *>(data);
  bool ok = data || !size;
  for (size_t offset = 0; ok && offset < size;) {
    const size_t chunk = std::min(size - offset, size_t{4096});
    ok = vault_sha256_process(&state, input + offset, chunk) == 0;
    offset += chunk;
    if (offset < size) vTaskDelay(1);
  }
  ok = ok && vault_sha256_done(&state, output) == 0;
  wipe(&state, sizeof(state));
  if (!ok) wipe(output, 32);
  return ok;
}
bool derive(const char *password, size_t length, const uint8_t salt[16],
            Key &output, const std::atomic<bool> &cancel) {
  wipe(output.bytes.data(), output.bytes.size());
  if (!validPin(password, length) || !salt || cancel.load() ||
      !sys::Power::isScreenOn() || sys::Power::sleepPending())
    return false;
  // PBKDF2 block 1 (dkLen=32). Explicit HMAC iteration permits cancellation
  // and idle/Wi-Fi scheduling; opaque long-running KDF calls would starve idle.
  uint8_t first[20]{};
  std::memcpy(first, salt, 16);
  first[19] = 1;
  Key u, next;
  HmacSha256 hmac;
  bool ok = hmac.init(password, length) &&
            hmac.compute(first, sizeof(first), u.bytes.data());
  output.bytes = u.bytes;
  for (uint32_t i = 1; ok && i < PinRounds; ++i) {
    if ((i & 255) == 0 && (cancel.load() || !sys::Power::isScreenOn() ||
                           sys::Power::sleepPending())) {
      ok = false;
      break;
    }
    if ((i & 1023) == 0)
      vTaskDelay(1);
    ok = hmac.compute(u.bytes.data(), u.bytes.size(), next.bytes.data());
    u.bytes = next.bytes;
    for (size_t j = 0; j < 32; ++j)
      output.bytes[j] ^= u.bytes[j];
  }
  wipe(first, sizeof(first));
  if (!ok || cancel.load() || !sys::Power::isScreenOn() ||
      sys::Power::sleepPending()) {
    wipe(output.bytes.data(), 32);
    return false;
  }
  return true;
}
bool gcm(bool encrypt, const Key &key, const uint8_t nonce[12],
         const uint8_t *aad, size_t aadSize, const uint8_t *input, size_t size,
         Bytes &output) {
  clear(output);
  if (size > MaxBundle || (!encrypt && size < VAULT_GCM_TAG_SIZE))
    return false;
  // Keep AES-GCM wire compatibility without hardware DMA/multipart timing.
  // Cooperative yields keep Wi-Fi/BLE/UI and idle tasks scheduled on large vaults.
  struct GcmContext {
    vault_gcm_aes256 value{};
    ~GcmContext() { vault_gcm_aes256_wipe(&value); }
  } holder;
  auto &context = holder.value;
  vault_gcm_aes256_setkey(&context, key.bytes.data());
  context.cooperate = [](void *) { vTaskDelay(1); };
  bool ok = false;
  if (encrypt) {
    output.resize(size + VAULT_GCM_TAG_SIZE);
    ok = vault_gcm_aes256_encrypt(&context, nonce, aad, aadSize, input, size,
                                  output.data(),
                                  output.data() + size) == 0;
  } else {
    const size_t body = size - VAULT_GCM_TAG_SIZE;
    uint8_t empty = 0;
    output.resize(body);
    ok = vault_gcm_aes256_decrypt(&context, nonce, aad, aadSize, input, body,
                                  body ? output.data() : &empty,
                                  input + body) == 0;
  }
  if (!ok) {
    clear(output);
    // Failure-only diagnostics: never log key, nonce, tag or plaintext.
    ESP_LOGW("vault", "AES-GCM %s failed: size=%u",
             encrypt ? "encrypt" : "decrypt", static_cast<unsigned>(size));
    return false;
  }
  return true;
}
bool decryptBundle(const Bytes &bundle, const Key &key, Bytes &plain) {
  if (!framing(bundle.data(), bundle.size()))
    return false;
  SecretBytes raw;
  Key data;
  if (!gcm(false, key, bundle.data() + 56, bundle.data(), 68,
           bundle.data() + 68, 48, raw.value) ||
      raw.value.size() != 32)
    return false;
  std::memcpy(data.bytes.data(), raw.value.data(), 32);
  return gcm(false, data, bundle.data() + 116, bundle.data(), 128,
             bundle.data() + 128, bundle.size() - 128, plain);
}
bool encryptEmpty(const Key &key, const uint8_t id[16], const uint8_t salt[16],
                  Bytes &bundle) {
  static constexpr char plain[] = "{\"schema\":2,\"entries\":[]}";
  bundle.assign(HeaderSize, 0);
  auto h = bundle.data();
  std::memcpy(h, "JYVAULT2", 8);
  write32(h + 12, sizeof(plain) - 1 + 16);
  std::memcpy(h + 24, id, 16);
  std::memcpy(h + 40, salt, 16);
  esp_fill_random(h + 56, 12);
  esp_fill_random(h + 116, 12);
  Key data;
  esp_fill_random(data.bytes.data(), 32);
  Bytes wrapped, cipher;
  if (!gcm(true, key, h + 56, h, 68, data.bytes.data(), 32, wrapped))
    return false;
  std::memcpy(h + 68, wrapped.data(), 48);
  if (!gcm(true, data, h + 116, h, 128,
           reinterpret_cast<const uint8_t *>(plain), sizeof(plain) - 1, cipher))
    return false;
  bundle.insert(bundle.end(), cipher.begin(), cipher.end());
  return framing(bundle.data(), bundle.size());
}
} // namespace vault
