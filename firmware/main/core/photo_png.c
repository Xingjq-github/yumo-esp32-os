#include "photo_png.h"
#include "png.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

enum { MaxDimension=16384, MaxSourcePixels=64*1024*1024, MaxCodecBytes=512*1024 };
typedef struct {
    const uint8_t *input;
    photo_png_read_fn read;
    void *input_context;
    size_t length, offset, used;
    uint8_t *row;
    photo_png_cancel_fn cancelled;
    void *context;
    int result;
} Reader;
typedef union { size_t size; long double alignment; void *pointer; } Allocation;

static void fail(png_structp png, png_const_charp message) {
    (void)message;
    png_longjmp(png,1);
}
static void warning(png_structp png, png_const_charp message) {
    Reader *r=png_get_mem_ptr(png);
    if(r && strstr(message,"exceeds user limit")) r->result=PHOTO_PNG_LIMIT;
}
static png_voidp allocate(png_structp png, png_alloc_size_t bytes) {
    Reader *r=png_get_mem_ptr(png);
    if(!r) return NULL;
    if(bytes>MaxCodecBytes || r->used>MaxCodecBytes-bytes) {r->result=PHOTO_PNG_MEMORY;return NULL;}
    Allocation *p=heap_caps_malloc(sizeof(*p)+bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p) {r->result=PHOTO_PNG_MEMORY;return NULL;}
    p->size=bytes; r->used+=bytes;
    return p+1;
}
static void release(png_structp png, png_voidp memory) {
    if(!memory) return;
    Reader *r=png_get_mem_ptr(png);
    Allocation *p=(Allocation *)memory-1;
    r->used-=p->size; heap_caps_free(p);
}
static void read_bytes(png_structp png, png_bytep output, png_size_t bytes) {
    Reader *r=png_get_io_ptr(png);
    if(r->cancelled && r->cancelled(r->context)) {r->result=PHOTO_PNG_CANCELLED;png_error(png,"cancelled");}
    if(bytes>r->length-r->offset) png_error(png,"truncated PNG");
    if(r->input) memcpy(output,r->input+r->offset,bytes);
    else if(!r->read(r->input_context,output,bytes)) png_error(png,"PNG read failed");
    r->offset+=bytes;
}
static void sample_row(uint8_t *dest, const uint8_t *row, unsigned width, unsigned out_width, int pass,const photo_view *view) {
    for(unsigned x=0;x<out_width;++x) {
        const unsigned sx=photo_sample((view?view->x:0)+x,width,view?view->scaled_width:out_width);
        // Adam7 writes only a subset of this row in each pass. Read only those
        // pixels, accumulating directly in the small output frame. No full-size
        // interlaced image allocation or stale pixels from another row.
        if(pass>=0 && (sx<PNG_PASS_START_COL(pass) ||
           (sx-PNG_PASS_START_COL(pass))%PNG_PASS_COL_OFFSET(pass))) continue;
        const uint8_t *p=row+(size_t)sx*3;
        memcpy(dest+x*3,p,3);
    }
}

static int decode(const uint8_t *input,photo_png_read_fn read,void *input_context,
                   size_t length, uint8_t *rgb,
                   uint16_t out_width, uint16_t out_height,
                   uint16_t *width, uint16_t *height,
                   photo_png_cancel_fn cancelled, void *context,const photo_view *view,photo_sink *sink) {
    if((!input && !read) || length<8 || length>16*1024*1024 || !width || !height ||
       (input && png_sig_cmp(input,0,8))) return 0;
    Reader *r=calloc(1,sizeof(*r));
    if(!r) return PHOTO_PNG_MEMORY;
    r->input=input;r->read=read;r->input_context=input_context;
    r->length=length;r->cancelled=cancelled;r->context=context;
    png_structp png=png_create_read_struct_2(PNG_LIBPNG_VER_STRING,NULL,fail,warning,r,allocate,release);
    if(!png) {free(r);return PHOTO_PNG_MEMORY;}
    png_infop info=png_create_info_struct(png);
    if(!info) {png_destroy_read_struct(&png,NULL,NULL);free(r);return PHOTO_PNG_MEMORY;}
    // Only heap-backed Reader state is read after longjmp. Automatic locals
    // assigned below are never used on that error path.
    if(setjmp(png_jmpbuf(png))) goto done;
    png_set_read_fn(png,r,read_bytes);
    png_set_user_limits(png,MaxDimension,MaxDimension);
    png_set_chunk_cache_max(png,16);
    png_set_chunk_malloc_max(png,64*1024);
    // Ignore unused metadata, including compressed text/profiles. Keep only
    // pixel-critical chunks and gamma; palette/transparency still transform.
    png_set_keep_unknown_chunks(png,PNG_HANDLE_CHUNK_NEVER,NULL,0);
    static const png_byte metadata[]="iCCP\0tEXt\0zTXt\0iTXt\0eXIf\0hIST\0sPLT\0pCAL\0sCAL\0bKGD\0tIME\0";
    png_set_keep_unknown_chunks(png,PNG_HANDLE_CHUNK_NEVER,metadata,(sizeof(metadata)-1)/5);
    png_read_info(png,info);
    const png_uint_32 w=png_get_image_width(png,info),h=png_get_image_height(png,info);
    const int depth=png_get_bit_depth(png,info),color=png_get_color_type(png,info);
    if(!w || !h || (uint64_t)w*h>MaxSourcePixels) {r->result=PHOTO_PNG_LIMIT;png_error(png,"PNG dimensions");}
    *width=(uint16_t)w;*height=(uint16_t)h;
    if(!rgb) {r->result=1;goto done;}
    if(view && !photo_view_valid(view,w,h)) {r->result=PHOTO_PNG_LIMIT;png_error(png,"PNG viewport");}
    if(!out_width || !out_height || out_width>w || out_height>h ||
       (size_t)out_width*out_height>1024*1024) {r->result=PHOTO_PNG_LIMIT;png_error(png,"PNG output dimensions");}
    if(depth==16) png_set_scale_16(png);
    if(color==PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if(color==PNG_COLOR_TYPE_GRAY && depth<8) png_set_expand_gray_1_2_4_to_8(png);
    if(png_get_valid(png,info,PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if(color==PNG_COLOR_TYPE_GRAY || color==PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    // Composite alpha onto the viewer's black background, with libpng's gamma
    // correction. sRGB is the default for untagged images.
    double gamma=0.45455; int intent=0;
    if(!png_get_sRGB(png,info,&intent)) png_get_gAMA(png,info,&gamma);
    png_set_gamma(png,2.2,gamma);
    png_color_16 black={0};
    png_set_background(png,&black,PNG_BACKGROUND_GAMMA_SCREEN,0,1.0);
    const int passes=png_set_interlace_handling(png);
    if(sink) sink->passes=(unsigned)passes;
    png_read_update_info(png,info);
    const size_t stride=(size_t)w*3;
    if(png_get_rowbytes(png,info)!=stride || png_get_channels(png,info)!=3 ||
       png_get_bit_depth(png,info)!=8) png_error(png,"PNG transform");
    r->row=heap_caps_calloc(1,stride,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!r->row) {r->result=PHOTO_PNG_MEMORY;png_error(png,"PNG row memory");}
    for(int pass=0;pass<passes;++pass) {
        unsigned dest_y=0,level_y=0;
        for(unsigned y=0;y<h;++y) {
            if(cancelled && cancelled(context)) {r->result=PHOTO_PNG_CANCELLED;png_error(png,"cancelled");}
            png_read_row(png,r->row,NULL);
            if(sink && level_y<view->scaled_height && y==photo_sample(level_y,h,view->scaled_height)) {
                const int active=passes==1 || (y>=PNG_PASS_START_ROW(pass) &&
                    (y-PNG_PASS_START_ROW(pass))%PNG_PASS_ROW_OFFSET(pass)==0);
                for(unsigned x=0;x<view->scaled_width;++x) {
                    unsigned sx=photo_sample(x,w,view->scaled_width);
                    sink->valid[x]=active && (passes==1 || (sx>=PNG_PASS_START_COL(pass) &&
                        (sx-PNG_PASS_START_COL(pass))%PNG_PASS_COL_OFFSET(pass)==0));
                    if(sink->valid[x]) memcpy(sink->row+x*3,r->row+sx*3,3);
                }
                sink->write(sink->context,level_y++,(unsigned)pass);
            }
            if(dest_y<out_height &&
               y==photo_sample((view?view->y:0)+dest_y,h,view?view->scaled_height:out_height)) {
                if(passes==1 || (y>=PNG_PASS_START_ROW(pass) &&
                   (y-PNG_PASS_START_ROW(pass))%PNG_PASS_ROW_OFFSET(pass)==0))
                    sample_row(rgb+(size_t)dest_y*out_width*3,r->row,w,out_width,passes==1?-1:pass,view);
                ++dest_y;
            }
        }
    }
    png_read_end(png,NULL);
    r->result=1;
done:
    png_destroy_read_struct(&png,&info,NULL);
    heap_caps_free(r->row);
    const int result=r->result;
    free(r);return result;
}
int photo_png_read(const uint8_t *input,size_t length,uint8_t *rgb,
                   uint16_t out_width,uint16_t out_height,
                   uint16_t *width,uint16_t *height,
                   photo_png_cancel_fn cancelled,void *context) {
    return decode(input,NULL,NULL,length,rgb,out_width,out_height,width,height,cancelled,context,NULL,NULL);
}
int photo_png_read_stream(photo_png_read_fn read,void *input_context,size_t length,
                   uint8_t *rgb,uint16_t out_width,uint16_t out_height,
                   uint16_t *width,uint16_t *height,
                   photo_png_cancel_fn cancelled,void *context) {
    return decode(NULL,read,input_context,length,rgb,out_width,out_height,width,height,cancelled,context,NULL,NULL);
}
int photo_png_read_view(const uint8_t *input,photo_png_read_fn read,void *input_context,
                   size_t length,uint8_t *rgb,const photo_view *view,
                   uint16_t *width,uint16_t *height,photo_png_cancel_fn cancelled,void *context) {
    if(rgb && !view) return PHOTO_PNG_LIMIT;
    return decode(input,read,input_context,length,rgb,view?view->width:0,view?view->height:0,
                  width,height,cancelled,context,view,NULL);
}
int photo_png_read_tiles(const uint8_t *input,photo_png_read_fn read,void *input_context,
                   size_t length,uint8_t *rgb,const photo_view *view,
                   uint16_t *width,uint16_t *height,photo_png_cancel_fn cancelled,void *context,photo_sink *sink) {
    if(!rgb || !view || !sink || !sink->row || !sink->valid || !sink->write) return PHOTO_PNG_INVALID;
    return decode(input,read,input_context,length,rgb,view->width,view->height,width,height,cancelled,context,view,sink);
}
