#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native little-endian RGB565, top to bottom. Callbacks run only on the writer
 * task. A failed callback aborts encoding and releases all codec allocations. */
typedef int (*screenshot_write_fn)(void *,const uint8_t *,size_t);
typedef int (*screenshot_cancel_fn)(void *);
int screenshot_png_write(const uint8_t *pixels,size_t length,
                         screenshot_write_fn write,screenshot_cancel_fn cancel,void *context);
#ifdef __cplusplus
}
#endif
