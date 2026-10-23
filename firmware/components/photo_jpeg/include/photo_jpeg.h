#pragma once
#include <stddef.h>
#include <stdint.h>
#include "photo_view.h"
#include "photo_sink.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { PHOTO_JPEG_OK=1, PHOTO_JPEG_INVALID=0, PHOTO_JPEG_MEMORY=-1,
       PHOTO_JPEG_LIMIT=-2, PHOTO_JPEG_CANCELLED=-3, PHOTO_JPEG_FORMAT=-4,
       PHOTO_JPEG_BACKING_LIMIT=-5, PHOTO_JPEG_IO=-6 };
enum { PHOTO_JPEG_MAX_BACKING=128*1024*1024 };
// Optional virtual-array storage, owned by the C++ worker. All callbacks return
// normally before libjpeg's C error handler can longjmp. Maximum three slots.
typedef struct {
    void *context;
    int (*open)(void *,size_t,unsigned *);
    int (*read)(void *,unsigned,size_t,uint8_t *,size_t);
    int (*write)(void *,unsigned,size_t,const uint8_t *,size_t);
    int (*close)(void *,unsigned);
} photo_jpeg_backing;
typedef int (*photo_jpeg_cancel_fn)(void *context);
// Return 1 only after reading exactly bytes; 0 aborts without a synthetic EOI.
// Callback must return before C error unwinding; caller retains input ownership.
typedef int (*photo_jpeg_read_fn)(void *context,uint8_t *output,size_t bytes);
// RGB888 output is caller-owned. NULL output reads only the header. All codec
// allocations use a per-call PSRAM budget; no filesystem or global decoder.
int photo_jpeg_read(const uint8_t *input,size_t length,uint8_t *rgb,
                    uint16_t out_width,uint16_t out_height,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context);
int photo_jpeg_read_stream(photo_jpeg_read_fn read,void *input_context,size_t length,
                    uint8_t *rgb,uint16_t out_width,uint16_t out_height,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context);
int photo_jpeg_read_view(const uint8_t *input,photo_jpeg_read_fn read,void *input_context,
                    size_t length,uint8_t *rgb,const photo_view *view,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context,const photo_jpeg_backing *backing);
int photo_jpeg_read_tiles(const uint8_t *input,photo_jpeg_read_fn read,void *input_context,
                    size_t length,uint8_t *rgb,const photo_view *view,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context,const photo_jpeg_backing *backing,photo_sink *sink);
#ifdef __cplusplus
}
#endif
