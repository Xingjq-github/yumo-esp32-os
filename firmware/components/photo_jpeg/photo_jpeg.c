#include "jpeg_port.h"
#include "jerror.h"
#include <stdlib.h>
#include <string.h>

static void failed(j_common_ptr jpeg) {
    photo_jpeg_reader *r=jpeg->client_data;
    if(jpeg->err->msg_code==JERR_OUT_OF_MEMORY) r->result=PHOTO_JPEG_MEMORY;
    longjmp(r->jump,1);
}
static void silent(j_common_ptr jpeg) {(void)jpeg;}
static void message(j_common_ptr jpeg,int level) {
    if(level<0) failed(jpeg); // Reject truncated or corrupt scans, never fake EOI.
}
static void checkpoint(j_common_ptr jpeg) {
    photo_jpeg_reader *r=jpeg->client_data;
    if(r->jpeg.input_scan_number>64) {r->result=PHOTO_JPEG_LIMIT;longjmp(r->jump,1);}
    if(r->cancel && r->cancel(r->context)) {r->result=PHOTO_JPEG_CANCELLED;longjmp(r->jump,1);}
}

// libjpeg source-manager protocol, as in upstream jdatasrc.c. The input window
// is charged to the codec budget; every refill and metadata skip can cancel.
typedef struct {
    struct jpeg_source_mgr pub;
    photo_jpeg_read_fn read;
    void *context;
    size_t remaining;
    JOCTET buffer[4096];
} Stream;
static void init_stream(j_decompress_ptr j) {(void)j;}
static void term_stream(j_decompress_ptr j) {(void)j;}
static boolean fill_stream(j_decompress_ptr j) {
    Stream *s=(Stream *)j->src;
    checkpoint((j_common_ptr)j);
    const size_t bytes=s->remaining<sizeof(s->buffer)?s->remaining:sizeof(s->buffer);
    if(!bytes || !s->read(s->context,s->buffer,bytes)) ERREXIT(j,JERR_INPUT_EOF);
    s->remaining-=bytes;s->pub.next_input_byte=s->buffer;s->pub.bytes_in_buffer=bytes;
    return TRUE;
}
static void skip_stream(j_decompress_ptr j,long bytes) {
    if(bytes<=0) return;
    while((size_t)bytes>j->src->bytes_in_buffer) {
        bytes-=(long)j->src->bytes_in_buffer;fill_stream(j);
    }
    j->src->next_input_byte+=(size_t)bytes;j->src->bytes_in_buffer-=(size_t)bytes;
}
static int decode(const uint8_t *input,photo_jpeg_read_fn read,void *input_context,
                    size_t length,uint8_t *rgb,
                    uint16_t out_width,uint16_t out_height,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context,const photo_view *view,
                    const photo_jpeg_backing *backing,photo_sink *sink) {
    if((!input && !read) || length<4 || length>16*1024*1024 || !width || !height || !progressive) return PHOTO_JPEG_INVALID;
    photo_jpeg_reader *r=calloc(1,sizeof(*r));if(!r) return PHOTO_JPEG_MEMORY;
    r->limit=photo_jpeg_available();if(r->limit>PHOTO_JPEG_MAX_CODEC) r->limit=PHOTO_JPEG_MAX_CODEC;
    r->cancel=cancel;r->context=context;r->backing=backing;r->jpeg.client_data=r;
    r->jpeg.err=jpeg_std_error(&r->error);r->error.error_exit=failed;
    r->error.output_message=silent;r->error.emit_message=message;
    // Longjmp stays inside this C boundary. All changed state is heap-backed.
    if(setjmp(r->jump)) goto done;
    jpeg_create_decompress(&r->jpeg);
    r->progress.progress_monitor=checkpoint;r->jpeg.progress=&r->progress;
    checkpoint((j_common_ptr)&r->jpeg);
    if(input) jpeg_mem_src(&r->jpeg,input,(unsigned long)length);
    else {
        Stream *s=(*r->jpeg.mem->alloc_small)((j_common_ptr)&r->jpeg,JPOOL_PERMANENT,sizeof(*s));
        memset(s,0,sizeof(*s));s->read=read;s->context=input_context;s->remaining=length;
        s->pub.init_source=init_stream;s->pub.fill_input_buffer=fill_stream;
        s->pub.skip_input_data=skip_stream;s->pub.term_source=term_stream;
        s->pub.resync_to_restart=jpeg_resync_to_restart;r->jpeg.src=&s->pub;
    }
    if(jpeg_read_header(&r->jpeg,TRUE)!=JPEG_HEADER_OK) goto done;
    struct jpeg_decompress_struct *j=&r->jpeg;
    if(j->data_precision!=8 || j->arith_code || (j->num_components!=1 && j->num_components!=3) ||
       (j->jpeg_color_space!=JCS_GRAYSCALE && j->jpeg_color_space!=JCS_RGB && j->jpeg_color_space!=JCS_YCbCr)) {r->result=PHOTO_JPEG_FORMAT;goto done;}
    if(!j->image_width || !j->image_height || j->image_width>16384 || j->image_height>16384 ||
       (uint64_t)j->image_width*j->image_height>(j->progressive_mode?32ull:64ull)*1024*1024) {
        r->result=PHOTO_JPEG_LIMIT;goto done;
    }
    *width=(uint16_t)j->image_width;*height=(uint16_t)j->image_height;*progressive=j->progressive_mode;
    if(!rgb) {r->result=PHOTO_JPEG_OK;goto done;}
    if(view && !photo_view_valid(view,j->image_width,j->image_height)) {r->result=PHOTO_JPEG_LIMIT;goto done;}
    if(!out_width || !out_height || out_width>j->image_width || out_height>j->image_height ||
       (size_t)out_width*out_height>1024*1024) {r->result=PHOTO_JPEG_LIMIT;goto done;}
    // Progressive coefficients use ORIGINAL resolution, padded to MCU blocks.
    // Reserve working rows/tables as well; the allocator remains the hard cap.
    size_t coefficients=0;
    for(int i=0;i<j->num_components;++i) {
        const jpeg_component_info *c=&j->comp_info[i];
        const size_t cols=((c->width_in_blocks+c->h_samp_factor-1)/c->h_samp_factor)*c->h_samp_factor;
        const size_t rows=((c->height_in_blocks+c->v_samp_factor-1)/c->v_samp_factor)*c->v_samp_factor;
        coefficients+=cols*rows*sizeof(JBLOCK);
    }
    if(j->progressive_mode && coefficients>PHOTO_JPEG_MAX_BACKING) {r->result=PHOTO_JPEG_BACKING_LIMIT;goto done;}
    if(j->progressive_mode && !backing && coefficients+384*1024>r->limit) {r->result=PHOTO_JPEG_MEMORY;goto done;}
    j->out_color_space=JCS_RGB;j->dct_method=JDCT_ISLOW;
    j->do_fancy_upsampling=TRUE;j->do_block_smoothing=FALSE;
    j->scale_num=1;j->scale_denom=8;
    for(;;) {
        jpeg_calc_output_dimensions(j);
        if((j->output_width>=(view?view->scaled_width:out_width) &&
            j->output_height>=(view?view->scaled_height:out_height)) || j->scale_denom==1) break;
        j->scale_denom/=2;
    }
    if(!jpeg_start_decompress(j) || j->output_components!=3) goto done;
    const unsigned native_width=j->output_width;
    unsigned crop_x=0;
    if(view && !sink) {
        crop_x=photo_sample(view->x,native_width,view->scaled_width);
        unsigned right=photo_sample(view->x+out_width-1,native_width,view->scaled_width)+1;
        // Preserve neighbouring chroma samples for fancy upsampling. Cropping
        // exactly at the visible edge would synthesize a false image boundary
        // and change its colours relative to the full-level cached result.
        const unsigned halo=2*j->max_h_samp_factor;
        crop_x=crop_x>halo?crop_x-halo:0;
        right=right+halo<native_width?right+halo:native_width;
        unsigned crop_width=right-crop_x;
        jpeg_crop_scanline(j,&crop_x,&crop_width);
    }
    JSAMPARRAY row=(*j->mem->alloc_sarray)((j_common_ptr)j,JPOOL_IMAGE,j->output_width*3,1);
    unsigned dy=0,level_y=0;
    if(sink) sink->passes=1;
    while(j->output_scanline<j->output_height) {
        checkpoint((j_common_ptr)j);
        const unsigned sy=j->output_scanline;
        if(jpeg_read_scanlines(j,row,1)!=1) goto done;
        if(sink && level_y<view->scaled_height && sy==photo_sample(level_y,j->output_height,view->scaled_height)) {
            for(unsigned x=0;x<view->scaled_width;++x) {
                unsigned sx=photo_sample(x,native_width,view->scaled_width);
                memcpy(sink->row+x*3,row[0]+sx*3,3);
            }
            sink->write(sink->context,level_y++,0);
        }
        if(dy<out_height && sy==photo_sample((view?view->y:0)+dy,j->output_height,view?view->scaled_height:out_height)) {
            uint8_t *dest=rgb+(size_t)dy*out_width*3;
            for(unsigned x=0;x<out_width;++x) {
                const unsigned sx=photo_sample((view?view->x:0)+x,native_width,view?view->scaled_width:out_width)-crop_x;
                memcpy(dest+x*3,row[0]+sx*3,3);
            }
            ++dy;
        }
    }
    if(dy==out_height && jpeg_finish_decompress(j)) r->result=PHOTO_JPEG_OK;
done:
    jpeg_destroy_decompress(&r->jpeg);
    const int result=r->result;free(r);return result;
}
int photo_jpeg_read(const uint8_t *input,size_t length,uint8_t *rgb,
                    uint16_t out_width,uint16_t out_height,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context) {
    return decode(input,NULL,NULL,length,rgb,out_width,out_height,width,height,progressive,cancel,context,NULL,NULL,NULL);
}
int photo_jpeg_read_stream(photo_jpeg_read_fn read,void *input_context,size_t length,
                    uint8_t *rgb,uint16_t out_width,uint16_t out_height,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context) {
    return decode(NULL,read,input_context,length,rgb,out_width,out_height,width,height,progressive,cancel,context,NULL,NULL,NULL);
}
int photo_jpeg_read_view(const uint8_t *input,photo_jpeg_read_fn read,void *input_context,
                    size_t length,uint8_t *rgb,const photo_view *view,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context,const photo_jpeg_backing *backing) {
    if(rgb && !view) return PHOTO_JPEG_LIMIT;
    return decode(input,read,input_context,length,rgb,view?view->width:0,view?view->height:0,
                  width,height,progressive,cancel,context,view,backing,NULL);
}
int photo_jpeg_read_tiles(const uint8_t *input,photo_jpeg_read_fn read,void *input_context,
                    size_t length,uint8_t *rgb,const photo_view *view,
                    uint16_t *width,uint16_t *height,int *progressive,
                    photo_jpeg_cancel_fn cancel,void *context,const photo_jpeg_backing *backing,photo_sink *sink) {
    if(!rgb || !view || !sink || !sink->row || !sink->write) return PHOTO_JPEG_INVALID;
    return decode(input,read,input_context,length,rgb,view->width,view->height,
                  width,height,progressive,cancel,context,view,backing,sink);
}
