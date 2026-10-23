#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RECORDER_NS_FRAME 256
#define RECORDER_NS_ARENA (64*1024)
typedef struct RecorderDenoise RecorderDenoise;
// One foreground recorder only. All APIs are worker-only, never GUI callbacks.
RecorderDenoise *recorder_denoise_create(void);
void recorder_denoise_destroy(RecorderDenoise *ctx);
bool recorder_denoise_reset(RecorderDenoise *ctx);
// Exactly 256 samples (16ms). No heap allocation during processing.
bool recorder_denoise_run(RecorderDenoise *ctx,int16_t *pcm);
bool recorder_denoise_voice(const RecorderDenoise *ctx);
size_t recorder_denoise_used(const RecorderDenoise *ctx);
#ifdef __cplusplus
}
#endif
