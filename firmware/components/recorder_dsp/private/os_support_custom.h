#pragma once
#include <stddef.h>
void *recorder_speex_alloc(int size);
#define OVERRIDE_SPEEX_ALLOC
#define OVERRIDE_SPEEX_ALLOC_SCRATCH
#define OVERRIDE_SPEEX_REALLOC
#define OVERRIDE_SPEEX_FREE
#define OVERRIDE_SPEEX_FREE_SCRATCH
#define speex_alloc(size) recorder_speex_alloc(size)
#define speex_alloc_scratch(size) recorder_speex_alloc(size)
#define speex_realloc(ptr,size) recorder_speex_alloc(-1)
#define speex_free(ptr) ((void)(ptr))
#define speex_free_scratch(ptr) ((void)(ptr))
