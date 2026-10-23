// Upstream minimp3: ea99364f61c14656440e8d77e9c233ccf3124633 (CC0).
// Bound each seek-index allocation; compressed files and PCM are streamed.
#include <stdlib.h>
#include "esp_heap_caps.h"
static void *mp3_alloc(size_t n) {
    return n <= 2 * 1024 * 1024 ? heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
}
static void *mp3_realloc(void *p, size_t n) {
    return n <= 2 * 1024 * 1024 ? heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
}
#define malloc mp3_alloc
#define realloc mp3_realloc
#define free heap_caps_free
#define MINIMP3_SCAN_LIMIT (256 * 1024)
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
