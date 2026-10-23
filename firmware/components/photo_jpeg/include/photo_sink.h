#pragma once
#include <stdint.h>
// Optional full-scale row consumer. Buffers belong to the worker, not the codec.
// Adam7 emits all scaled rows on every pass; valid[x] selects changed pixels.
// A failed optional cache disables itself without interrupting viewport decode.
typedef struct {
    void *context;
    uint8_t *row, *valid;
    unsigned passes;
    void (*write)(void *,uint32_t,unsigned);
} photo_sink;
