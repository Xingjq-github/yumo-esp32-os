#pragma once
#include <stddef.h>
#include <stdint.h>
#include "photo_view.h"
#include "photo_sink.h"
#ifdef __cplusplus
extern "C" {
#endif
// libpng's longjmp is contained in C; no C++ objects or SD handles cross it.
// RGB888 output is owned by the caller and already bounded to one megapixel.
typedef int (*photo_png_cancel_fn)(void *context);
typedef int (*photo_png_read_fn)(void *context,uint8_t *output,size_t bytes);
enum { PHOTO_PNG_INVALID=0, PHOTO_PNG_OK=1, PHOTO_PNG_MEMORY=-1,
       PHOTO_PNG_LIMIT=-2, PHOTO_PNG_CANCELLED=-3, PHOTO_PNG_FORMAT=-4 };
int photo_png_read(const uint8_t *input, size_t length, uint8_t *rgb,
                   uint16_t out_width, uint16_t out_height,
                   uint16_t *width, uint16_t *height,
                   photo_png_cancel_fn cancelled, void *context);
int photo_png_read_stream(photo_png_read_fn read,void *input_context,size_t length,
                   uint8_t *rgb,uint16_t out_width,uint16_t out_height,
                   uint16_t *width,uint16_t *height,
                   photo_png_cancel_fn cancelled,void *context);
int photo_png_read_view(const uint8_t *input,photo_png_read_fn read,void *input_context,
                   size_t length,uint8_t *rgb,const photo_view *view,
                   uint16_t *width,uint16_t *height,photo_png_cancel_fn cancelled,void *context);
int photo_png_read_tiles(const uint8_t *input,photo_png_read_fn read,void *input_context,
                   size_t length,uint8_t *rgb,const photo_view *view,
                   uint16_t *width,uint16_t *height,photo_png_cancel_fn cancelled,void *context,photo_sink *sink);
#ifdef __cplusplus
}
#endif
