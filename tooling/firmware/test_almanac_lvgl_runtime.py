"""Native LVGL replay of production rolling text and extracted clock geometry."""
from pathlib import Path
import argparse,subprocess,re,json,hashlib
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--run-dir',type=Path,required=True);args=p.parse_args()
    run=args.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    lv=ROOT/'../dependencies/managed_components/lvgl__lvgl';source=(ROOT/'main/ui/almanac_clock.cpp').read_text(encoding='utf8')
    layout=source.split('void AlmanacClock::create(lv_obj_t *parent) {',1)[1].split('\n}',1)[0]
    layout=layout.replace('area=compact::panel','lv_obj_t *area=panel').replace('auto middle=compact::panel','lv_obj_t *middle=panel')
    layout=re.sub(r'rows\[(\d)\]\.create\(',r'ui_rolling_create(&rows[\1],',layout).replace('theme::','theme_')
    palette=re.findall(r'return \{([^}]+)\};',(ROOT/'main/core/theme_model.hpp').read_text())
    (run/'geometry.h').write_text('static const unsigned palettes[5][11]={'+','.join('{'+r+'}' for r in palette)+'};\nstatic void layout(lv_obj_t *parent){'+layout+'\n}\n',encoding='utf8')
    (run/'lv_conf.h').write_text('''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (4*1024*1024U)
#define LV_USE_OS LV_OS_NONE
#define LV_FONT_MONTSERRAT_18 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_ASSERT_OBJ 1
#define LV_USE_ASSERT_STYLE 1
#define LV_ASSERT_HANDLER {abort();}
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#endif
''')
    (run/'replay.c').write_text(r'''
#include "lvgl.h"
#include "ui/rolling_text.h"
#include "ui/rolling_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(lv_font_almanac_24);
LV_FONT_DECLARE(lv_font_cjk_16);
LV_FONT_DECLARE(lv_font_menu_14);
static lv_font_t small;
static ui_rolling_text_t rows[7];
static ui_rolling_clock_t clock;
static unsigned palette_index,checks;
static const unsigned *colors;
static unsigned char pixels[240*240*2],buf[240*240*2];
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static lv_color_t theme_surface(void){return lv_color_hex(colors[1]);}
static lv_color_t theme_fg(void){return lv_color_hex(colors[2]);}
static lv_color_t theme_accent(void){return lv_color_hex(colors[3]);}
static lv_color_t theme_muted(void){return lv_color_hex(colors[4]);}
static const lv_font_t *theme_fontSmall(void){return &small;}
static const lv_font_t *theme_fontMenu(void){return &lv_font_menu_14;}
static lv_obj_t *panel(lv_obj_t *parent,int w,int h){lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_size(o,w,h);lv_obj_set_scrollable(o,false);lv_obj_set_style_bg_color(o,lv_color_hex(colors[0]),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);return o;}
#include "geometry.h"
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 for(int y=a->y1;y<=a->y2;++y)memcpy(pixels+(y*240+a->x1)*2,p+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);lv_display_flush_ready(d);
}
static void capture(const char *name){lv_refr_now(NULL);FILE *f=fopen(name,"wb");fprintf(f,"P6\n240 240\n255\n");
 for(int i=0;i<240*240;++i){unsigned p=pixels[i*2]+256*pixels[i*2+1];unsigned char rgb[]={((p>>11)*255)/31,(((p>>5)&63)*255)/63,((p&31)*255)/31};fwrite(rgb,1,3,f);}fclose(f);}
static unsigned objects(lv_obj_t *o){unsigned n=1;for(unsigned i=0;i<lv_obj_get_child_count(o);++i)n+=objects(lv_obj_get_child(o,i));return n;}
static void advance(unsigned ms){for(unsigned n=0;n<ms;n+=20){lv_tick_inc(20);lv_timer_handler();}}
int main(void){
 lv_init();lv_display_t *d=lv_display_create(240,240);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(d,buf,NULL,sizeof(buf),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
 small=lv_font_cjk_16;small.line_height=22;small.base_line=5;small.fallback=&lv_font_montserrat_18;
 const char *values[]={"农历 丙午年  生肖马","八月十四","节气 秋分","申时  哺时","丙午 丁酉 辛丑 丙申","时星神  司命","天干:阳火 地支:阳金 纳音:山下火","十五时三十六分十五秒"};
 for(palette_index=0;palette_index<5;++palette_index){
  colors=palettes[palette_index];memset(rows,0,sizeof(rows));layout(lv_screen_active());
  for(int i=0;i<7;++i)ui_rolling_set(&rows[i],values[i],false);ui_rolling_clock_set(&clock,15,36,15,false);
  lv_obj_update_layout(lv_screen_active());unsigned before=objects(lv_screen_active());
  for(int i=0;i<7;++i){
   CHECK(lv_obj_get_x(rows[i].box)==4);CHECK(lv_obj_get_width(rows[i].box)==232);
   CHECK(lv_obj_get_y(rows[i].box)+lv_obj_get_height(rows[i].box)<=240);
   CHECK(rows[i].offset>=0);CHECK(!rows[i].active);
   CHECK(lv_obj_get_y(rows[i].labels[0])==rows[i].offset);
   CHECK(lv_obj_get_y(rows[i].labels[1])>=rows[i].height);
   lv_point_t size;lv_text_get_size(&size,values[i],lv_obj_get_style_text_font(rows[i].labels[0],0),0,0,1000,LV_TEXT_FLAG_NONE);CHECK(size.x<=232);
  }
  char file[40];sprintf(file,"theme-%u.ppm",palette_index);capture(file);
  ui_rolling_set(&rows[3],"酉时  日入",true);ui_rolling_set(&rows[4],"丙午 丁酉 辛丑 丁酉",true);
  advance(140);CHECK(rows[3].active&&rows[4].active);CHECK(lv_obj_get_y(rows[3].labels[0])<rows[3].offset);
  sprintf(file,"transition-%u.ppm",palette_index);capture(file);
  advance(220);CHECK(!rows[3].active&&!rows[4].active);
  CHECK(strcmp(lv_label_get_text(rows[3].labels[0]),"酉时  日入")==0);

  ui_rolling_clock_set(&clock,15,36,16,true);
  for(int n=0;n<12;++n)CHECK(clock.cells[n].active==(n==10));
  advance(140);CHECK(lv_obj_get_y(clock.cells[10].labels[0])<clock.cells[10].offset);
  for(int n=0;n<12;++n)if(n!=10)CHECK(lv_obj_get_y(clock.cells[n].labels[0])==clock.cells[n].offset);
  sprintf(file,"character-%u.ppm",palette_index);capture(file);advance(400);
  ui_rolling_clock_set(&clock,9,9,9,false);ui_rolling_clock_set(&clock,9,9,10,true);
  for(int n=0;n<12;++n)CHECK(clock.cells[n].active==(n==9||n==10));advance(400);
  for(unsigned n=0;n<240;++n){
   ui_rolling_clock_set(&clock,n%24,n%60,(n+1)%60,true);advance(20);
   ui_rolling_clock_set(&clock,n%24,n%60,(n+2)%60,true);ui_rolling_clock_stop(&clock);
   CHECK(objects(lv_screen_active())==before);
   for(int k=0;k<12;++k){CHECK(!clock.cells[k].active);CHECK(lv_anim_get(&clock.cells[k],ui_rolling_animate)==NULL);}
  }
  ui_rolling_clock_set(&clock,23,59,59,false);ui_rolling_clock_set(&clock,0,0,0,true);
  for(int k=3;k<12;k+=4)CHECK(!clock.cells[k].active);advance(80);ui_rolling_clock_stop(&clock);
  ui_rolling_clock_clear(&clock);for(int k=0;k<12;++k)CHECK(!*lv_label_get_text(clock.cells[k].labels[0]));
  for(int i=0;i<7;++i)ui_rolling_set(&rows[i],values[i],true);
  advance(60);for(int i=0;i<7;++i)ui_rolling_stop(&rows[i]);ui_rolling_clock_stop(&clock);
  lv_obj_clean(lv_screen_active());advance(400);CHECK(objects(lv_screen_active())==1);
  CHECK(lv_anim_count_running()==0);
 }
 lv_display_delete(d);lv_deinit();printf("Almanac LVGL: %u checks, five palettes, mid-animation replacement/stop/destroy, constant object counts\n",checks);return 0;
}
''',encoding='utf8')
    folders=('core','display','draw','indev','layouts','misc','osal','stdlib','themes','tick','widgets','libs/bin_decoder')
    files=[lv/'src/lv_init.c']+[p for f in folders for p in (lv/'src'/f).rglob('*.c')]
    files += [p for p in (lv/'src/font').rglob('*.c') if 'montserrat' not in p.name or p.name in ('lv_font_montserrat_14.c','lv_font_montserrat_18.c')]
    files += [ROOT/'main/ui/lv_font_almanac_24.c',ROOT/'main/ui/lv_font_cjk_16.c',ROOT/'main/ui/lv_font_menu_14.c',run/'replay.c']
    cmd=['-g','-bt','10','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(lv),'-I'+str(ROOT/'main'),'-o',str(run/'replay.exe')]+list(map(str,files))
    (run/'compile.rsp').write_text(subprocess.list2cmdline(cmd),encoding='utf8')
    for stage,command in [('compile',[str(args.compiler.resolve()),'@'+str(run/'compile.rsp')]),('execute',[str(run/'replay.exe')])]:
        proc=subprocess.run(command,cwd=run,capture_output=True,timeout=180);(run/(stage+'.log')).write_bytes(proc.stdout+proc.stderr)
        if proc.returncode:print((proc.stdout+proc.stderr).decode(errors='replace'));raise RuntimeError(stage+' failed')
    result={'passed':True,'result':proc.stdout.decode().strip(),'method':'production C animation, extracted C++ layout, real LVGL; firmware lifecycle checked separately',
            'inputs':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ('main/ui/rolling_text.h','main/ui/rolling_clock.h','main/ui/almanac_clock.cpp','main/ui/lv_font_almanac_24.c','main/ui/lv_font_cjk_16.c','main/ui/lv_font_menu_14.c')}}
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(result['result'])
if __name__=='__main__':main()
