"""Native LVGL capture + production PNG writer/album decoder, synthetic data only."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--run-dir',type=Path,required=True)
    args=parser.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    lv=ROOT/'../dependencies/managed_components/lvgl__lvgl'
    png=ROOT/'../dependencies/managed_components/espressif__libpng'
    zlib=ROOT/'../dependencies/managed_components/espressif__zlib/zlib'
    source=(ROOT/'main/core/screenshot.cpp').read_text(encoding='utf8')
    capture='static '+source.split('void capture(lv_event_t *event) {',1)[1].split('\nNotice finish(',1)[0]
    capture=capture.replace('static ','static void capture(lv_event_t *event) {',1)
    capture=capture.replace('const auto area=static_cast<const lv_area_t *>(lv_event_get_param(event));',
                            'const lv_area_t *area=(const lv_area_t *)lv_event_get_param(event);')
    capture=capture.replace('auto buffer=','lv_draw_buf_t *buffer=')
    assert 'auto ' not in capture and 'static_cast' not in capture
    (run/'capture.h').write_text(capture,encoding='utf8')
    (run/'lv_conf.h').write_text('''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (4*1024*1024U)
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_ASSERT_OBJ 1
#define LV_ASSERT_HANDLER {abort();}
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#endif
''',encoding='utf8')
    (run/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t,unsigned);
void *heap_caps_calloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
''',encoding='utf8')
    (run/'replay.c').write_text(r'''
#include "lvgl.h"
#include "screenshot_png.h"
#include "photo_png.h"
#include "display_cast_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static unsigned checks,live,allocations,failAllocation;
static size_t bytes,peak;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef union {size_t size;long double align;} Allocation;
void *heap_caps_malloc(size_t n,unsigned flags) {
 (void)flags;if(++allocations==failAllocation)return NULL;
 Allocation *a=malloc(sizeof(*a)+n);if(!a)return NULL;
 a->size=n;++live;bytes+=n;if(bytes>peak)peak=bytes;return a+1;
}
void *heap_caps_calloc(size_t a,size_t b,unsigned flags){void *p=heap_caps_malloc(a*b,flags);if(p)memset(p,0,a*b);return p;}
void heap_caps_free(void *p){if(!p)return;Allocation *a=(Allocation*)p-1;CHECK(live>0);--live;bytes-=a->size;free(a);}
static lv_display_t *display;
static uint8_t panel[YMCAST_BYTES],cast[YMCAST_BYTES],pixels[YMCAST_BYTES],buf1[240*13*2],buf2[240*13*2];
static bool allowed=true;
typedef struct {uint8_t *pixels;bool rows[YMCAST_HEIGHT];unsigned rowCount;bool captured,invalid,writing;} Job;
static Job storage,*job;
static bool eligible(void){return allowed;}
#include "capture.h"
static void cast_capture(lv_event_t *event) {
 const lv_area_t *a=lv_event_get_param(event);lv_draw_buf_t *b=lv_display_get_buf_active(display);
 CHECK(ymcast_copy_rect(cast,a->x1,a->y1,a->x2,a->y2,b->data,b->header.stride,b->data_size));
}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {
 lv_draw_buf_t *b=lv_display_get_buf_active(d);
 CHECK(ymcast_copy_rect(panel,a->x1,a->y1,a->x2,a->y2,p,b->header.stride,b->data_size));
 // Simulate the SPI adapter modifying RGB565 byte order AFTER capture.
 lv_draw_sw_rgb565_swap(p,lv_area_get_size(a));lv_display_flush_ready(d);
}
static void begin(void){memset(&storage,0,sizeof(storage));memset(pixels,0xcd,sizeof(pixels));storage.pixels=pixels;job=&storage;}
static lv_obj_t *box(lv_obj_t *root,int x,int y,int w,int h,unsigned color) {
 lv_obj_t *o=lv_obj_create(root);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);return o;
}
static uint8_t output[256*1024],rgb[240*240*3];
static size_t used;static unsigned writes,calls,failWrite,failCancel;
static int write_bytes(void *ctx,const uint8_t *data,size_t n){
 (void)ctx;if(++writes==failWrite)return 0;CHECK(used+n<=sizeof(output));memcpy(output+used,data,n);used+=n;return 1;
}
static int cancel(void *ctx){(void)ctx;return ++calls==failCancel;}
static int encode(void){used=0;writes=0;calls=0;return screenshot_png_write(pixels,sizeof(pixels),write_bytes,cancel,NULL);}
static void save_file(const char *path,const void *p,size_t n){FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(p,1,n,f)==n);CHECK(!fclose(f));}
int main(void) {
 lv_init();display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,buf1,buf2,sizeof(buf1),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 lv_display_add_event_cb(display,capture,LV_EVENT_FLUSH_START,NULL);
 lv_display_add_event_cb(display,cast_capture,LV_EVENT_FLUSH_START,NULL);
 lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0x184c78),0);
 box(lv_screen_active(),0,0,70,70,0xff0000);
 lv_obj_t *blue=box(lv_screen_active(),170,170,70,70,0x0000ff);
 box(lv_layer_top(),200,0,40,25,0x00ff00);box(lv_layer_sys(),210,220,30,20,0xffffff);
 lv_refr_now(NULL);
 for(unsigned iteration=0;iteration<30;++iteration) {
  begin();lv_obj_set_style_bg_color(blue,lv_color_hex(iteration%2?0xffaa00:0x0000ff),0);
  lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
  CHECK(job->captured && !job->invalid && job->rowCount==240);
  CHECK(!memcmp(panel,pixels,sizeof(panel)));CHECK(!memcmp(cast,pixels,sizeof(cast)));
  const size_t i=(10*240+220)*2;CHECK(pixels[i]==0xe0 && pixels[i+1]==7); // top layer green
  CHECK(pixels[sizeof(pixels)-2]==255 && pixels[sizeof(pixels)-1]==255); // system layer white
 }
 CHECK(encode()==1);CHECK(!live && !bytes);
 save_file("capture.png",output,used);save_file("capture.rgb565",pixels,sizeof(pixels));
 uint16_t w=0,h=0;CHECK(photo_png_read(output,used,rgb,240,240,&w,&h,NULL,NULL)==1);CHECK(w==240 && h==240);CHECK(!live && !bytes);
 CHECK(rgb[0]==255 && rgb[1]==0 && rgb[2]==0);
 // Once captured the immutable frame is not overwritten by later redraws.
 uint8_t frozen[YMCAST_BYTES];memcpy(frozen,pixels,sizeof(pixels));
 lv_obj_set_style_bg_color(blue,lv_color_hex(0xff00ff),0);lv_refr_now(NULL);CHECK(!memcmp(frozen,pixels,sizeof(pixels)));
 // Partial/inhibited frames never become ready for the SD task.
 begin();lv_obj_invalidate(blue);lv_refr_now(NULL);CHECK(!job->captured && job->invalid);
 begin();allowed=false;lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);CHECK(!job->captured && job->rowCount==0);
 allowed=true;job=NULL;lv_display_delete(display);lv_deinit();
 // Every RGB565 channel and pixel orientation survives the lossless encoder.
 for(unsigned frame=0;frame<2;++frame) {
  for(unsigned i=0;i<240*240;++i){unsigned c=i+frame*57600;pixels[i*2]=c;pixels[i*2+1]=c>>8;}
  CHECK(encode()==1);char name[32];sprintf(name,"palette-%u.png",frame);save_file(name,output,used);
  CHECK(photo_png_read(output,used,rgb,240,240,&w,&h,NULL,NULL)==1);CHECK(!live && !bytes);
 }
 // Force every allocator failure in the codec and verify complete cleanup.
 allocations=0;CHECK(encode()==1);const unsigned total=allocations;
 for(unsigned i=1;i<=total;++i){allocations=0;failAllocation=i;CHECK(encode()==0);CHECK(!live && !bytes);}
 failAllocation=0;CHECK(encode()==1);const unsigned count=writes;
 for(unsigned i=1;i<=count;++i){failWrite=i;CHECK(encode()==0);CHECK(!live && !bytes);}
 failWrite=0;
 for(unsigned i=1;i<250;i+=13){failCancel=i;CHECK(encode()==0);CHECK(!live && !bytes);}
 failCancel=0;CHECK(encode()==1);CHECK(!live && !bytes);
 CHECK(screenshot_png_write(NULL,sizeof(pixels),write_bytes,NULL,NULL)==0);
 CHECK(screenshot_png_write(pixels,2,write_bytes,NULL,NULL)==0);
 printf("Screenshot: %u assertions; full partial-buffer frame, both overlays, SPI swap, USB coexistence, PNG/album read, %u allocation failures; peak codec bytes=%zu\n",checks,total,peak);
 return 0;
}
''',encoding='utf8')
    folders=('core','display','draw','indev','layouts','misc','osal','stdlib','themes','tick','widgets','libs/bin_decoder')
    files=[lv/'src/lv_init.c']+[p for f in folders for p in (lv/'src'/f).rglob('*.c')]
    files += [p for p in (lv/'src/font').rglob('*.c') if 'montserrat' not in p.name or p.name=='lv_font_montserrat_14.c']
    files += [p for p in (png/'libpng').glob('png*.c') if p.name!='pngtest.c']
    files += [zlib/(name+'.c') for name in ('adler32','compress','crc32','deflate','infback','inffast','inflate','inftrees','trees','uncompr','zutil')]
    files += [ROOT/'main/core/screenshot_png.c',ROOT/'main/core/photo_png.c',ROOT/'main/core/display_cast_codec.c',run/'replay.c']
    flags=['-g','-bt','10','-DLV_CONF_INCLUDE_SIMPLE','-DPNGCAPI=','-DPNG_USER_PRIVATEBUILD=1']
    flags += ['-I'+str(p) for p in (run,lv,ROOT/'main/core',png,png/'libpng',zlib)]
    command=flags+['-o',str(run/'replay.exe')]+list(map(str,files))
    (run/'compile.rsp').write_text(subprocess.list2cmdline(command),encoding='utf8')
    for stage,cmd in [('compile',[str(args.compiler.resolve()),'@'+str(run/'compile.rsp')]),('execute',[str(run/'replay.exe')])]:
        proc=subprocess.run(cmd,cwd=run,capture_output=True,timeout=180)
        (run/(stage+'.log')).write_bytes(proc.stdout+proc.stderr)
        if proc.returncode:print((proc.stdout+proc.stderr).decode(errors='replace'));raise RuntimeError(stage+' failed')
    # Independent standards decoder also verifies every pixel and orientation.
    import sys
    from PIL import Image
    def expected(raw):
        rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]+(raw[i+1]<<8);r=c>>11;g=(c>>5)&63;b=c&31
            rgb.extend(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)))
        return bytes(rgb)
    image=Image.open(run/'capture.png');assert image.size==(240,240)
    assert image.convert('RGB').tobytes()==expected((run/'capture.rgb565').read_bytes())
    for frame in range(2):
        raw=b''.join(((i+frame*57600)&65535).to_bytes(2,'little') for i in range(57600))
        assert Image.open(run/f'palette-{frame}.png').convert('RGB').tobytes()==expected(raw)
    inputs=['main/core/screenshot.cpp','main/core/screenshot_png.c','main/core/screenshot_png.h',
            'main/core/display_cast_codec.c','main/core/photo_png.c','main/core/button_mgr.cpp','main/apps/photo_viewer.cpp']
    report={'passed':True,'result':proc.stdout.decode().strip(),'independent_decoder_pixels':3*57600,
            'method':'production PNG writer+album decoder; extracted production capture with real LVGL; host allocator fault injection',
            'inputs':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}}
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(report['result'])
if __name__=='__main__':main()
