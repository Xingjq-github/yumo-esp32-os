#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *fc_alloc(size_t size);
void *fc_calloc(size_t count,size_t size);
void *fc_calloc_hot(size_t count,size_t size);
uint16_t fc_color(unsigned index);
#ifdef __cplusplus
}
#endif
