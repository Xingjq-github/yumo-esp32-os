/* Frame-boundary checkpoints. No heap addresses or callback pointers on disk.
 * Version is a compatibility contract: bump it if emulation semantics or the
 * serialized fields change. Layout also checks field order, sizes and ABI. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct nes_s nes_t;
typedef struct {
    uint8_t *bytes;
    size_t position, capacity;
    uint32_t layout;
    int mode; /* 0 measure, 1 save, 2 validate, 3 commit */
    bool valid;
} checkpoint_io_t;
void checkpoint_field(checkpoint_io_t *, void *, size_t, const char *);
#define CHECKPOINT_FIELD(io, field) checkpoint_field(io, &(field), sizeof(field), #field)
uint32_t checkpoint_crc(const void *bytes, size_t length);
/* CRC running state starts at ~0u; complement after the last chunk. */
uint32_t checkpoint_crc_update(uint32_t state, const void *bytes, size_t length);
/* Immutable per-cartridge identity. Prepared on the NES owner thread once.
 * Capture copies live state only; seal works exclusively on detached bytes. */
typedef struct { uint32_t header[16]; size_t length; } checkpoint_profile_t;
bool checkpoint_prepare(nes_t *, checkpoint_profile_t *);
bool checkpoint_capture(nes_t *, const checkpoint_profile_t *, void *, size_t);
bool checkpoint_seal(void *, size_t);
size_t checkpoint_size(nes_t *nes);
bool checkpoint_save(nes_t *nes, void *bytes, size_t length);
bool checkpoint_valid(nes_t *nes, const void *bytes, size_t length);
bool checkpoint_load(nes_t *nes, const void *bytes, size_t length);
