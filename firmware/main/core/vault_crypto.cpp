#include "vault_crypto.hpp"
#include "vault_gcm.h"
#include "vault_sha256.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_mgr.hpp"
#include "psa/crypto.h"
#include <algorithm>
#include <cstring>
namespace vault {
namespace {
struct PsaKey {
  mbedtls_svc_key_id_t id = 0;
  ~PsaKey() {
    if (id)
      psa_destroy_key(id);
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
  if (!validPin(password, length) || psa_crypto_init() != PSA_SUCCESS)
    return false;
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
  PsaKey key;
  auto rc = psa_import_key(&attr, reinterpret_cast<const uint8_t *>(password),
                           length, &key.id);
  psa_reset_key_attributes(&attr);
  if (rc != PSA_SUCCESS)
    return false;
  // PBKDF2 block 1 (dkLen=32). Explicit HMAC iteration permits cancellation
  // and idle/Wi-Fi scheduling; opaque long-running KDF calls would starve idle.
  uint8_t first[20]{};
  std::memcpy(first, salt, 16);
  first[19] = 1;
  Key u, next;
  size_t got = 0;
  rc = psa_mac_compute(key.id, PSA_ALG_HMAC(PSA_ALG_SHA_256), first,
                       sizeof(first), u.bytes.data(), 32, &got);
  output.bytes = u.bytes;
  for (uint32_t i = 1; rc == PSA_SUCCESS && got == 32 && i < PinRounds; ++i) {
    if ((i & 255) == 0 && (cancel.load() || !sys::Power::isScreenOn() ||
                           sys::Power::sleepPending())) {
      rc = PSA_ERROR_BAD_STATE;
      break;
    }
    if ((i & 1023) == 0)
      vTaskDelay(1);
    rc = psa_mac_compute(key.id, PSA_ALG_HMAC(PSA_ALG_SHA_256), u.bytes.data(),
                         32, next.bytes.data(), 32, &got);
    u.bytes = next.bytes;
    for (size_t j = 0; j < 32; ++j)
      output.bytes[j] ^= u.bytes[j];
  }
  wipe(first, sizeof(first));
  if (rc != PSA_SUCCESS || got != 32 || cancel.load()) {
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
