#include "recorder_denoise.h"
#include "speex/speex_preprocess.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
struct RecorderDenoise {
  jmp_buf failure;
  SpeexPreprocessState *state;
  size_t used;
  unsigned voice_hold;
  // 16-byte aligned slices; the entire arena is allocated once in PSRAM.
  _Alignas(16) unsigned char arena[RECORDER_NS_ARENA];
};
// Access serialized by recorder::active. No other app links this private API.
static RecorderDenoise *allocating;
void *recorder_speex_alloc(int size) {
  if(!allocating)return NULL;
  if(size<=0 || (size_t)size>RECORDER_NS_ARENA-allocating->used)
    longjmp(allocating->failure,1);
  size_t rounded=((size_t)size+15)&~(size_t)15;
  if(rounded>RECORDER_NS_ARENA-allocating->used)longjmp(allocating->failure,1);
  void *ptr=allocating->arena+allocating->used;
  allocating->used+=rounded;
  memset(ptr,0,rounded);
  return ptr;
}
bool recorder_denoise_reset(RecorderDenoise *ctx) {
  if(!ctx)return false;
  ctx->state=NULL;ctx->used=0;ctx->voice_hold=0;
  // Upstream init assumes allocations succeed. This C-only boundary catches
  // arena exhaustion before any null dereference; no C++ frame is bypassed.
  allocating=ctx;
  if(setjmp(ctx->failure)) {allocating=NULL;ctx->state=NULL;return false;}
  ctx->state=speex_preprocess_state_init(RECORDER_NS_FRAME,16000);
  int on=1,off=0,attenuation=-12;
  int result=speex_preprocess_ctl(ctx->state,SPEEX_PREPROCESS_SET_DENOISE,&on);
  result|=speex_preprocess_ctl(ctx->state,SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,&attenuation);
  result|=speex_preprocess_ctl(ctx->state,SPEEX_PREPROCESS_SET_AGC,&off);
  // VAD and echo cancellation remain disabled by upstream defaults.
  allocating=NULL;
  if(result)ctx->state=NULL;
  return ctx->state!=NULL;
}
RecorderDenoise *recorder_denoise_create(void) {
#ifdef ESP_PLATFORM
  RecorderDenoise *ctx=heap_caps_aligned_calloc(16,1,sizeof(*ctx),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  RecorderDenoise *ctx=calloc(1,sizeof(*ctx));
#endif
  if(ctx&&!recorder_denoise_reset(ctx)) {free(ctx);ctx=NULL;}
  return ctx;
}
void recorder_denoise_destroy(RecorderDenoise *ctx) {
  // All Speex objects belong to this arena, with no external handles.
  free(ctx);
}
bool recorder_denoise_run(RecorderDenoise *ctx,int16_t *pcm) {
  if(!ctx||!ctx->state||!pcm)return false;
  allocating=ctx;
  if(setjmp(ctx->failure)) {allocating=NULL;ctx->state=NULL;return false;}
  speex_preprocess_run(ctx->state,pcm);
  int probability=0;
  speex_preprocess_ctl(ctx->state,SPEEX_PREPROCESS_GET_PROB,&probability);
  if(probability>=35)ctx->voice_hold=12;
  else if(ctx->voice_hold)--ctx->voice_hold;
  allocating=NULL;
  return true;
}
bool recorder_denoise_voice(const RecorderDenoise *ctx) {return ctx&&ctx->voice_hold>0;}
size_t recorder_denoise_used(const RecorderDenoise *ctx) {return ctx?ctx->used:0;}
