#pragma once
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include "jpeglib.h"
#include "photo_jpeg.h"
enum { PHOTO_JPEG_MAX_CODEC=2*1024*1024, PHOTO_JPEG_RESERVE=384*1024 };
typedef struct {
    struct jpeg_decompress_struct jpeg;
    struct jpeg_error_mgr error;
    struct jpeg_progress_mgr progress;
    jmp_buf jump;
    size_t used,limit;
    int result;
    photo_jpeg_cancel_fn cancel;
    void *context;
    const photo_jpeg_backing *backing;
    size_t backing_bytes;
} photo_jpeg_reader;
size_t photo_jpeg_available(void);
