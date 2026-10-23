"""Run the local LVGL layout engine on the host, including face-switch anchors.

Exercises production hand creation and dial positioning statements. Hardware
services and the C++ app lifecycle are not part of this isolated C harness.
"""
from pathlib import Path
import argparse,json,re,subprocess,hashlib
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--run-dir',type=Path,required=True);args=p.parse_args()
    run=args.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    lv=ROOT/'../dependencies/managed_components/lvgl__lvgl'
    source=(ROOT/'main/apps/watchface.cpp').read_text(encoding='utf8')
    hand=source.split('static lv_obj_t *createHand(',1)[1].split('static void setHand(',1)[0]
    hand='static lv_obj_t *createHand('+hand
    position=source.split('    m_dialPixels=size;',1)[1].split('    lv_obj_set_style_text_font(m_centerDate',1)[0]
    position='    m_dialPixels=size;'+position
    # These are the actual branch assignments, not a second layout specification.
    rects=[tuple(map(int,r)) for r in re.findall(r'x=(\d+);y=(\d+);size=(\d+);',source)]
    assert len(rects)==9,rects
    config=(lv/'lv_conf_template.h').read_text(encoding='utf8').replace('#if 0 /* Set this to "1" to enable content */','#if 1')
    config=re.sub(r'#define LV_MEM_SIZE [^\n]+','#define LV_MEM_SIZE (4 * 1024 * 1024U)',config)
    config=re.sub(r'(#define LV_FONT_MONTSERRAT_(?:10|14|18|24)\s+)0',r'\g<1>1',config)
    (run/'lv_conf.h').write_text(config,encoding='utf8')
    harness=r'''
#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
static unsigned checks=0;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}++checks;}while(0)
static unsigned char pixels[240*240*2],buf[240*240*2];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {
    for(int y=a->y1;y<=a->y2;++y)memcpy(pixels+(y*240+a->x1)*2,p+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);
    lv_display_flush_ready(d);
}
static void capture(const char *name) {
    lv_refr_now(NULL);FILE *f=fopen(name,"wb");fprintf(f,"P6\n240 240\n255\n");
    for(int i=0;i<240*240;++i) {unsigned p=pixels[i*2]+256*pixels[i*2+1];unsigned char rgb[]={((p>>11)*255)/31,(((p>>5)&63)*255)/63,((p&31)*255)/31};fwrite(rgb,1,3,f);}fclose(f);
}
HAND
static void place_dial(lv_obj_t *m_dial,int x,int y,int size) {
    int m_dialPixels;
POSITION
}
int main(void) {
    lv_init();lv_display_t *display=lv_display_create(240,240);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buf,NULL,sizeof(buf),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
    lv_display_set_theme(display,lv_theme_default_init(display,lv_color_hex(0xd5ef8d),lv_color_hex(0xd5ef8d),true,&lv_font_montserrat_14));
    lv_obj_t *area=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(area);lv_obj_set_size(area,240,240);
    lv_obj_set_style_bg_color(area,lv_color_hex(0x101e1b),0);lv_obj_set_style_bg_opa(area,LV_OPA_COVER,0);
    lv_obj_set_scrollable(area,false);
    lv_obj_t *dial=lv_obj_create(area);lv_obj_remove_style_all(dial);lv_obj_set_size(dial,240,240);lv_obj_set_scrollable(dial,false);
    lv_obj_set_style_pad_all(dial,0,0);lv_obj_set_style_border_width(dial,0,0);
    lv_obj_t *hub=lv_obj_create(dial);lv_obj_remove_style_all(hub);lv_obj_set_size(hub,10,10);lv_obj_center(hub);
    lv_obj_set_style_radius(hub,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(hub,lv_color_hex(0xb8d6bd),0);lv_obj_set_style_bg_opa(hub,LV_OPA_COVER,0);
    lv_point_precise_t points[60][2]={0},hp[3][2]={0};lv_obj_t *ticks[60],*hands[3];
    for(int i=0;i<60;++i)ticks[i]=createHand(dial,points[i],i%5?1:3,lv_color_hex(i%5?0x405b42:0xb8d6bd));
    for(int i=0;i<3;++i)hands[i]=createHand(dial,hp[i],i==0?7:i==1?4:2,lv_color_hex(i==2?0xd5ef8d:0xb8d6bd));
    lv_obj_t *caption=lv_label_create(area);lv_label_set_text(caption,"SEC");
    const int rects[][3]={RECTS};
    // Reproduce the old bug against real LVGL before checking the correction.
    lv_obj_set_size(dial,144,144);lv_obj_center(dial);lv_obj_set_pos(dial,94,70);lv_obj_update_layout(area);
    CHECK(lv_obj_get_x(dial)==142 && lv_obj_get_y(dial)==118);
    CHECK(lv_obj_get_x(dial)+lv_obj_get_width(dial)>240);
    for(int round=0;round<4;++round)for(unsigned r=0;r<sizeof(rects)/sizeof(rects[0]);++r) {
        const int x=rects[r][0],y=rects[r][1],size=rects[r][2],center=size/2;
        // A previous face deliberately left CENTER state behind.
        lv_obj_center(dial);place_dial(dial,x,y,size);lv_obj_update_layout(area);
        CHECK(lv_obj_get_x(dial)==x && lv_obj_get_y(dial)==y);
        CHECK(lv_obj_get_x(dial)+size<=240 && lv_obj_get_y(dial)+size<=240);
        CHECK(lv_obj_get_x(hub)+5==center && lv_obj_get_y(hub)+5==center);
        for(int i=0;i<60;++i){
            double a=(i*6.0-90.0)*3.141592653589793/180.0;int outer=center-2,inner=outer-(i%5?2:7);
            points[i][0]=(lv_point_precise_t){center+cos(a)*inner,center+sin(a)*inner};points[i][1]=(lv_point_precise_t){center+cos(a)*outer,center+sin(a)*outer};
            lv_line_set_points(ticks[i],points[i],2);
        }
        for(int angle=0;angle<360;angle+=6){
            for(int i=0;i<3;++i){double a=(angle+i*17-90)*3.141592653589793/180.0;int len=i==0?center/2:i==1?center*7/10:center-14,tail=i==0?6:i==1?8:10;
                hp[i][0]=(lv_point_precise_t){center-(int)(cos(a)*tail),center-(int)(sin(a)*tail)};hp[i][1]=(lv_point_precise_t){center+(int)(cos(a)*len),center+(int)(sin(a)*len)};
                lv_line_set_points(hands[i],hp[i],2);
            }
            lv_obj_update_layout(area);
            for(int i=0;i<3;++i){CHECK(lv_obj_get_x(hands[i])==0 && lv_obj_get_y(hands[i])==0);CHECK(hp[i][1].x>=0&&hp[i][1].y>=0&&hp[i][1].x<size&&hp[i][1].y<size);}
        }
        for(int i=0;i<60;++i){CHECK(lv_obj_get_x(ticks[i])==0 && lv_obj_get_y(ticks[i])==0);}
        lv_obj_align(caption,LV_ALIGN_CENTER,-80,106);lv_obj_set_style_transform_rotation(caption,round*900,0);
        lv_obj_set_style_transform_rotation(caption,0,0);lv_obj_align(caption,LV_ALIGN_TOP_LEFT,15,210);lv_obj_update_layout(area);
        CHECK(lv_obj_get_x(caption)==15&&lv_obj_get_y(caption)==210);
        if(round==0){char name[64];sprintf(name,"dial-%u.ppm",r);capture(name);}
    }
    lv_display_delete(display);lv_deinit();printf("LVGL clock geometry: %u checks passed; reproduced old offset, verified 9 layouts x 4 transitions x 60 angles\n",checks);return 0;
}
'''.replace('HAND',hand).replace('POSITION',position).replace('RECTS',','.join('{%d,%d,%d}'%r for r in rects))
    src=run/'clock_runtime.c';src.write_text(harness,encoding='utf8')
    files=[p for p in (lv/'src').rglob('*.c') if not set(p.relative_to(lv/'src').parts)&{'libs','drivers'}]
    files+=list((lv/'src/libs/bin_decoder').glob('*.c'))
    exe=run/'clock_runtime.exe'
    cmd=[str(args.compiler.resolve()),'-I'+str(run),'-I'+str(lv),'-DLV_CONF_INCLUDE_SIMPLE','-o',str(exe),str(src)]+[str(f) for f in files]
    rsp=run/'compile.rsp';rsp.write_text('\n'.join('"'+v.replace('\\','/')+'"' for v in cmd[1:]),encoding='utf8')
    stages=[]
    for name,command in [('compile',[cmd[0],'@'+str(rsp)]),('execute',[str(exe)])]:
        proc=subprocess.run(command,cwd=run,capture_output=True,timeout=150)
        (run/(name+'.stdout.log')).write_bytes(proc.stdout);(run/(name+'.stderr.log')).write_bytes(proc.stderr)
        stages.append({'stage':name,'exit_code':proc.returncode})
        if proc.returncode:
            print(proc.stderr.decode(errors='replace'));print(proc.stdout.decode(errors='replace'));raise RuntimeError(name+' failed')
    result={'passed':True,'stages':stages,'result':proc.stdout.decode().strip(),'watchface_sha256':hashlib.sha256(source.encode()).hexdigest(),'method':'production dial-placement and createHand snippets against local LVGL; not complete C++ app/hardware'}
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(result['result'])

if __name__=='__main__':main()
