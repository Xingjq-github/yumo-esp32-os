#include "photo_gif.h"
#include "giflib/gif_lib.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *pixels_alloc(size_t bytes) {return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
#else
static void *pixels_alloc(size_t bytes) {return malloc(bytes);}
#endif

struct photo_gif {
    void *context;
    photo_gif_read_cb read;
    photo_gif_seek_cb rewind;
    photo_gif_cancel_cb cancel;
    GifFileType *file;
    uint16_t *canvas, *saved;
    uint8_t *line;
    uint16_t source_x[240],palette[256];
    unsigned width,height,source_w,source_h;
    unsigned left,top,right,bottom,disposal;
    uint16_t background;
    int transparent_background,first,cancelled;
};
static int stopped(photo_gif *g) {
    if(g->cancel && g->cancel(g->context))g->cancelled=1;
    return g->cancelled;
}
static int read_data(GifFileType *file,GifByteType *bytes,int count) {
    photo_gif *g=file->UserData;
    if(count<0 || stopped(g))return 0;
    return g->read(g->context,bytes,count);
}
static uint16_t color(const GifColorType *c) {
    return (uint16_t)(((c->Red>>3)<<11)|((c->Green>>2)<<5)|(c->Blue>>3));
}
static unsigned edge(unsigned value,unsigned dest,unsigned source) {
    return (value*dest+source-1)/source;
}
static int open_stream(photo_gif *g) {
    int error=0;
    g->file=DGifOpen(g,read_data,&error);
    if(!g->file)return 0;
    unsigned w=g->file->SWidth,h=g->file->SHeight;
    if(!w||!h||w>1024||h>1024 || (g->source_w&&(w!=g->source_w||h!=g->source_h)))return 0;
    g->source_w=w;g->source_h=h;
    g->width=w>=h?240:(w*240/h);g->height=h>=w?240:(h*240/w);
    if(!g->width)g->width=1;
    if(!g->height)g->height=1;
    for(unsigned x=0;x<g->width;++x)g->source_x[x]=(uint16_t)(x*w/g->width);
    g->background=0;
    if(g->file->SColorMap && g->file->SBackGroundColor>=0 &&
       g->file->SBackGroundColor<g->file->SColorMap->ColorCount)
        g->background=color(&g->file->SColorMap->Colors[g->file->SBackGroundColor]);
    g->first=1;g->disposal=0;g->transparent_background=0;
    return 1;
}
photo_gif *photo_gif_open(void *context,photo_gif_read_cb read,
    photo_gif_seek_cb rewind,photo_gif_cancel_cb cancel) {
    if(!read||!rewind)return NULL;
    photo_gif *g=calloc(1,sizeof(*g));if(!g)return NULL;
    g->context=context;g->read=read;g->rewind=rewind;g->cancel=cancel;
    if(!open_stream(g)) {photo_gif_close(g);return NULL;}
    const size_t size=g->width*g->height*sizeof(uint16_t);
    g->canvas=pixels_alloc(size);g->line=malloc(g->source_w);
    if(!g->canvas||!g->line) {photo_gif_close(g);return NULL;}
    return g;
}
void photo_gif_close(photo_gif *g) {
    if(!g)return;
    if(g->file)DGifCloseFile(g->file,NULL);
    free(g->canvas);free(g->saved);free(g->line);free(g);
}
unsigned photo_gif_width(const photo_gif *g) {return g?g->width:0;}
unsigned photo_gif_height(const photo_gif *g) {return g?g->height:0;}
int photo_gif_restart(photo_gif *g) {
    if(!g||stopped(g))return 0;
    DGifCloseFile(g->file,NULL);g->file=NULL;
    return g->rewind(g->context)==0 && open_stream(g);
}
static int draw_line(photo_gif *g,unsigned y,const ColorMapObject *map,int transparent) {
    GifImageDesc *im=&g->file->Image;
    if(stopped(g))return 0;
    if(DGifGetLine(g->file,g->line,im->Width)==GIF_ERROR)return 0;
    unsigned sy=im->Top+y;
    unsigned begin=edge(sy,g->height,g->source_h),end=edge(sy+1,g->height,g->source_h);
    for(unsigned dy=begin;dy<end && dy<g->height;++dy) {
        for(unsigned dx=g->left;dx<g->right;++dx) {
            unsigned sx=g->source_x[dx]-im->Left;
            unsigned index=g->line[sx];
            if((int)index==transparent)continue;
            if(index>=(unsigned)map->ColorCount)return 0;
            g->canvas[dy*g->width+dx]=g->palette[index];
        }
    }
    return 1;
}
int photo_gif_next(photo_gif *g,uint16_t *out,size_t pixels,unsigned *delay) {
    if(!g||!g->file||!out||pixels<g->width*g->height)return -1;
    if(stopped(g))return -2;
    const size_t bytes=g->width*g->height*sizeof(uint16_t);
    if(g->disposal==3)memcpy(g->canvas,g->saved,bytes);
    else if(g->disposal==2) {
        uint16_t bg=g->transparent_background?0:g->background;
        for(unsigned y=g->top;y<g->bottom;++y)
            for(unsigned x=g->left;x<g->right;++x)g->canvas[y*g->width+x]=bg;
    }
    GraphicsControlBlock control={0,false,10,NO_TRANSPARENT_COLOR};
    for(;;) {
        GifRecordType type;
        if(stopped(g))return -2;
        if(DGifGetRecordType(g->file,&type)==GIF_ERROR)return g->cancelled?-2:-1;
        if(type==TERMINATE_RECORD_TYPE)return 0;
        if(type==EXTENSION_RECORD_TYPE) {
            int code;GifByteType *data;
            if(DGifGetExtension(g->file,&code,&data)==GIF_ERROR)return -1;
            if(code==GRAPHICS_EXT_FUNC_CODE) {
                if(!data||data[0]!=4||DGifExtensionToGCB(data[0],data+1,&control)==GIF_ERROR)return -1;
                if(control.DisposalMode>3)return -1;
            }
            while(data) {
                if(stopped(g))return -2;
                if(DGifGetExtensionNext(g->file,&data)==GIF_ERROR)return -1;
            }
            continue;
        }
        if(type!=IMAGE_DESC_RECORD_TYPE||DGifGetImageHeader(g->file)==GIF_ERROR)return -1;
        GifImageDesc *im=&g->file->Image;
        if(im->Left<0||im->Top<0||im->Width<=0||im->Height<=0||
           (unsigned)(im->Left+im->Width)>g->source_w||
           (unsigned)(im->Top+im->Height)>g->source_h)return -1;
        const ColorMapObject *map=im->ColorMap?im->ColorMap:g->file->SColorMap;
        if(!map||!map->Colors||map->ColorCount<=0||map->ColorCount>256)return -1;
        // Convert each palette entry once per frame, including local palettes.
        // Pixel loops use lookups only; no per-pixel division/RGB conversion.
        for(int i=0;i<map->ColorCount;++i)g->palette[i]=color(&map->Colors[i]);
        if(g->first) {
            const uint16_t bg=control.TransparentColor==NO_TRANSPARENT_COLOR?g->background:0;
            for(size_t i=0;i<g->width*g->height;++i)g->canvas[i]=bg;
            g->first=0;
        }
        if(control.DisposalMode==3) {
            if(!g->saved)g->saved=pixels_alloc(bytes);
            if(!g->saved)return -1;
            memcpy(g->saved,g->canvas,bytes);
        }
        g->left=edge(im->Left,g->width,g->source_w);
        g->right=edge(im->Left+im->Width,g->width,g->source_w);
        g->top=edge(im->Top,g->height,g->source_h);
        g->bottom=edge(im->Top+im->Height,g->height,g->source_h);
        if(im->Interlace) {
            static const unsigned start[4]={0,4,2,1},step[4]={8,8,4,2};
            for(unsigned p=0;p<4;++p)for(unsigned y=start[p];y<(unsigned)im->Height;y+=step[p])
                if(!draw_line(g,y,map,control.TransparentColor))return g->cancelled?-2:-1;
        } else for(unsigned y=0;y<(unsigned)im->Height;++y)
            if(!draw_line(g,y,map,control.TransparentColor))return g->cancelled?-2:-1;
        g->disposal=control.DisposalMode;
        g->transparent_background=control.TransparentColor!=NO_TRANSPARENT_COLOR;
        memcpy(out,g->canvas,bytes);
        if(delay)*delay=control.DelayTime>1?(unsigned)control.DelayTime*10:100;
        return 1;
    }
}
