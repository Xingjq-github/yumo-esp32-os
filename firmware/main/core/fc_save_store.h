#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { FC_SAVE_MANUAL_SLOTS=3, FC_SAVE_AUTO_SLOT=3, FC_SAVE_SLOTS=4, FC_SAVE_MAX=256*1024 };
typedef struct {
    bool exists, valid;
    uint8_t bank;
    uint64_t revision, timestamp, frames;
    uint32_t rate, crc;
} fc_save_slot_t;
typedef struct {
    const char *directory;
    uint8_t rom[32];
    uint8_t *buffer; /* caller-owned bounded scratch; payload on read/write */
    size_t length;
    void *context;
    bool (*proceed)(void *context); /* cancellation and scheduler yield */
    bool (*validate)(void *context,const void *payload,size_t length);
    bool (*capture)(void *context,void *payload,size_t length);
    int error; /* errno; EBADMSG invalid, ESTALE changed, ECANCELED cancelled */
} fc_save_store_t;
bool fc_save_path(const fc_save_store_t *,unsigned slot,unsigned bank,char *path,size_t size);
bool fc_save_scan(fc_save_store_t *,fc_save_slot_t slots[FC_SAVE_SLOTS]);
bool fc_save_inspect_slot(fc_save_store_t *,unsigned slot,fc_save_slot_t *);
bool fc_save_same(const fc_save_slot_t *,const fc_save_slot_t *);
/* expected comes from the displayed/confirmed list. Never overwrites a changed
 * selection. Writes the other generation; the valid old file stays untouched. */
bool fc_save_write(fc_save_store_t *,unsigned slot,const fc_save_slot_t *expected,
                   uint64_t timestamp,uint64_t frames,uint32_t rate,fc_save_slot_t *result);
bool fc_save_read(fc_save_store_t *,unsigned slot,const fc_save_slot_t *expected,fc_save_slot_t *result);
/* Worker-only automatic save: immutable sealed snapshot, 4096-byte IO scratch
 * in store.buffer. Streaming checks never call the live emulator callbacks. */
bool fc_save_write_snapshot(fc_save_store_t *,const uint8_t *snapshot,uint64_t timestamp,
                            uint64_t frames,uint32_t rate,fc_save_slot_t *result);
#ifdef __cplusplus
}
#endif
