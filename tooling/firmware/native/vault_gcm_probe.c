/* Host-only entry for production AES-GCM; all inputs are synthetic fixtures. */
#include "vault_gcm.h"
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static void cooperate(void *arg) { ++*(unsigned *)arg; }
API int probe(int encrypt, const uint8_t *key, const uint8_t *nonce,
              const uint8_t *aad, unsigned aad_len, const uint8_t *input,
              unsigned input_len, uint8_t *output, uint8_t *tag, unsigned *yields) {
  vault_gcm_aes256 ctx;
  int result;
  unsigned i;
  vault_gcm_aes256_setkey(&ctx, key);
  ctx.cooperate=cooperate;ctx.arg=yields;*yields=0;
  if(encrypt)result=vault_gcm_aes256_encrypt(&ctx,nonce,aad,aad_len,input,input_len,output,tag);
  else result=vault_gcm_aes256_decrypt(&ctx,nonce,aad,aad_len,input,input_len,output,tag);
  vault_gcm_aes256_wipe(&ctx);
  for(i=0;i<sizeof(ctx);++i)if(((uint8_t *)&ctx)[i])return -100;
  return result;
}
