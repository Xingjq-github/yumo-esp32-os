#pragma once
// Deterministic software AES-256-GCM for the vault partition.
//
// Avoids the hardware driver's DMA/interrupt and multipart-buffer constraints.
// Bounded callers may install a cooperative scheduler callback for long buffers.
// Wire format and authentication tags remain AES-256-GCM compatible.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VAULT_GCM_TAG_SIZE 16
typedef struct {
  uint32_t round[60];
  void (*cooperate)(void *arg);
  void *arg;
} vault_gcm_aes256;
void vault_gcm_aes256_setkey(vault_gcm_aes256 *ctx, const uint8_t key[32]);
void vault_gcm_aes256_wipe(vault_gcm_aes256 *ctx);
// 0 on success: writes input_len bytes of ciphertext and the 16-byte tag.
int vault_gcm_aes256_encrypt(const vault_gcm_aes256 *ctx, const uint8_t nonce[12],
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *input, size_t input_len,
                             uint8_t *output, uint8_t tag[VAULT_GCM_TAG_SIZE]);
// 0 on success, 1 when the tag does not authenticate; output is then zeroed.
int vault_gcm_aes256_decrypt(const vault_gcm_aes256 *ctx, const uint8_t nonce[12],
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *input, size_t input_len,
                             uint8_t *output, const uint8_t tag[VAULT_GCM_TAG_SIZE]);
#ifdef __cplusplus
}
#endif
