#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
    YMCAST_WIDTH=240, YMCAST_HEIGHT=240, YMCAST_PIXELS=240*240,
    YMCAST_BYTES=240*240*2, YMCAST_HEADER=36,
    YMCAST_FRAME=1, YMCAST_STATUS=2, YMCAST_END=3,
    YMCAST_HELLO=16, YMCAST_READY=17, YMCAST_ACK=18, YMCAST_STOP=19,
    YMCAST_RAW=0, YMCAST_RLE=1
};
typedef struct {
    uint8_t kind,format,flags;
    uint32_t session,sequence,length,crc;
} ymcast_packet;
uint32_t ymcast_crc32(const uint8_t *data,size_t length);
void ymcast_header(uint8_t out[YMCAST_HEADER],const ymcast_packet *packet);
int ymcast_parse(const uint8_t header[YMCAST_HEADER],ymcast_packet *packet);
/* Lossless RGB565 PackBits: low 7 bits + 1 pixels; high bit repeats one pixel.
 * Returns 0 when raw encoding is smaller, or on insufficient output space. */
size_t ymcast_encode(const uint8_t *pixels,size_t count,uint8_t *out,size_t capacity);
int ymcast_decode(const uint8_t *data,size_t length,uint8_t *out,size_t pixels);
/* Copy a partial LVGL buffer before the panel driver swaps RGB565 byte order. */
int ymcast_copy_rect(uint8_t *canvas,int x1,int y1,int x2,int y2,
                     const uint8_t *source,size_t stride,size_t source_size);
#ifdef __cplusplus
}
#endif
