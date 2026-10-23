#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private, allocation-free software SHA-256. No PSA state or DMA buffers. */
typedef struct vault_sha256_state {
    uint64_t length;
    uint32_t state[8], curlen;
    uint8_t buf[64];
} vault_sha256_state;
void vault_sha256_init(vault_sha256_state *md);
int vault_sha256_process(vault_sha256_state *md, const unsigned char *in,
                         unsigned long inlen);
int vault_sha256_done(vault_sha256_state *md, unsigned char *out);
#ifdef __cplusplus
}
#endif
