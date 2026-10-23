#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct photo_gif photo_gif;
typedef int (*photo_gif_read_cb)(void *, uint8_t *, int);
typedef int (*photo_gif_seek_cb)(void *); // rewind to byte zero; 0 on success
typedef int (*photo_gif_cancel_cb)(void *); // cooperative yield/cancellation
// Decoder owns only a bounded scanline and two screen-sized composition buffers.
photo_gif *photo_gif_open(void *context, photo_gif_read_cb read,
    photo_gif_seek_cb rewind, photo_gif_cancel_cb cancel);
void photo_gif_close(photo_gif *gif);
unsigned photo_gif_width(const photo_gif *gif);
unsigned photo_gif_height(const photo_gif *gif);
// 1=frame, 0=trailer, -1=invalid/IO, -2=cancelled. No all-frame accumulation.
int photo_gif_next(photo_gif *gif, uint16_t *rgb565, size_t pixels, unsigned *delay_ms);
int photo_gif_restart(photo_gif *gif);
#ifdef __cplusplus
}
#endif
