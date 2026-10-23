#pragma once
#include "vault_format.hpp"
#include <array>
#include <atomic>
#include <string>
#include <vector>
namespace vault {
using Bytes = std::vector<uint8_t>;
inline void clear(Bytes &v) {
  if (!v.empty())
    wipe(v.data(), v.size());
  v.clear();
}
inline void clear(std::string &v) {
  if (!v.empty())
    wipe(v.data(), v.size());
  v.clear();
}
struct SecretBytes {
  Bytes value;
  ~SecretBytes() { clear(value); }
};
struct Key {
  std::array<uint8_t, 32> bytes{};
  ~Key() { wipe(bytes.data(), bytes.size()); }
};
bool hash(const void *data, size_t size, uint8_t output[32]);
bool derive(const char *password, size_t length, const uint8_t salt[16],
            Key &output, const std::atomic<bool> &cancel);
bool gcm(bool encrypt, const Key &key, const uint8_t nonce[12],
         const uint8_t *aad, size_t aadSize, const uint8_t *input, size_t size,
         Bytes &output);
bool encryptEmpty(const Key &key, const uint8_t id[16], const uint8_t salt[16],
                  Bytes &bundle);
bool decryptBundle(const Bytes &bundle, const Key &key, Bytes &plain);
} // namespace vault
