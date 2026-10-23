#include "screenshot_png.h"
#include "display_cast_codec.h"
#include "png.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

enum { MaxCodecBytes=192*1024 };
typedef struct {
    screenshot_write_fn write;
    screenshot_cancel_fn cancel;
    void *context;
    size_t used;
    int result;
    uint8_t row[YMCAST_WIDTH*3];
} Writer;
typedef union {size_t size;long double alignment;void *pointer;} Allocation;
static void fail(png_structp png,png_const_charp message) {(void)message;png_longjmp(png,1);}
static void warning(png_structp png,png_const_charp message) {(void)png;(void)message;}
static png_voidp allocate(png_structp png,png_alloc_size_t bytes) {
    Writer *w=png_get_mem_ptr(png);
    if(!w || bytes>MaxCodecBytes || w->used>MaxCodecBytes-bytes)return NULL;
    Allocation *p=heap_caps_malloc(sizeof(*p)+bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p)return NULL;
    p->size=bytes;w->used+=bytes;return p+1;
}
static void release(png_structp png,png_voidp memory) {
    if(!memory)return;
    Writer *w=png_get_mem_ptr(png);Allocation *p=(Allocation *)memory-1;
    w->used-=p->size;
    memset(memory,0,p->size);heap_caps_free(p);
}
static void write_bytes(png_structp png,png_bytep data,png_size_t length) {
    Writer *w=png_get_io_ptr(png);
    if((w->cancel && w->cancel(w->context)) || !w->write(w->context,data,length))
        png_error(png,"screenshot write failed");
}
int screenshot_png_write(const uint8_t *pixels,size_t length,
                         screenshot_write_fn write,screenshot_cancel_fn cancel,void *context) {
    if(!pixels || length!=YMCAST_BYTES || !write)return 0;
    Writer *w=calloc(1,sizeof(*w));if(!w)return 0;
    w->write=write;w->cancel=cancel;w->context=context;
    png_structp png=png_create_write_struct_2(PNG_LIBPNG_VER_STRING,NULL,fail,warning,w,allocate,release);
    if(!png) {free(w);return 0;}
    png_infop info=png_create_info_struct(png);
    if(!info) {png_destroy_write_struct(&png,NULL);free(w);return 0;}
    // State observed after longjmp lives on the heap, outside C++ RAII scopes.
    if(setjmp(png_jmpbuf(png)))goto done;
    png_set_write_fn(png,w,write_bytes,NULL);
    png_set_IHDR(png,info,YMCAST_WIDTH,YMCAST_HEIGHT,8,PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB(png,info,PNG_sRGB_INTENT_PERCEPTUAL);
    png_set_compression_level(png,1);
    png_set_compression_mem_level(png,4);
    png_set_compression_window_bits(png,12);
    png_set_filter(png,PNG_FILTER_TYPE_BASE,PNG_FILTER_SUB);
    png_write_info(png,info);
    for(unsigned y=0;y<YMCAST_HEIGHT;++y) {
        if(cancel && cancel(context))png_error(png,"screenshot cancelled");
        for(unsigned x=0;x<YMCAST_WIDTH;++x) {
            const size_t i=((size_t)y*YMCAST_WIDTH+x)*2;
            const uint16_t c=(uint16_t)pixels[i]|((uint16_t)pixels[i+1]<<8);
            const unsigned r=c>>11,g=(c>>5)&63,b=c&31;
            w->row[x*3]=(uint8_t)((r<<3)|(r>>2));
            w->row[x*3+1]=(uint8_t)((g<<2)|(g>>4));
            w->row[x*3+2]=(uint8_t)((b<<3)|(b>>2));
        }
        png_write_row(png,w->row);
    }
    png_write_end(png,info);w->result=1;
done:
    png_destroy_write_struct(&png,&info);
    const int result=w->result;
    memset(w,0,sizeof(*w));free(w);return result;
}
