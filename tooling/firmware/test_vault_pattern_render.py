"""Replay the production pattern view against local LVGL's RGB565 renderer.

No device access. Compares each partial refresh with a forced full redraw of
the same state. Hardware power, palette selection and label helpers are stubs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "lvgl.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <random>
struct Disc { lv_area_t area; lv_opa_t opacity; };
static std::vector<Disc> discs;
static std::vector<lv_area_t> requested_dirty;
static void observed_invalidate(lv_obj_t *o,const lv_area_t *a) {
    requested_dirty.push_back(*a);lv_obj_invalidate_area(o,a);
}
static void observed_rect(lv_layer_t *l,const lv_draw_rect_dsc_t *d,const lv_area_t *a) {
    discs.push_back({*a,d->bg_opa}); lv_draw_rect(l,d,a);
}
#define lv_draw_rect observed_rect
#define lv_obj_invalidate_area observed_invalidate
#include "production/vault_pattern.cpp"
#undef lv_draw_rect
#undef lv_obj_invalidate_area
static unsigned checks=0,frames=0,submissions=0;
static unsigned quantized_frames=0,quantized_pixels=0;
static unsigned long long partial_pixels=0,full_pixels=0,flush_pixels=0;
static unsigned char pixels[240*240*2],buffer[240*20*2],buffer2[240*20*2];
static bool pressed=false;
static int px=0,py=0;
static int old_px=0,old_py=0;
static void fail(const char *s,int line) {fprintf(stderr,"line %d: %s\n",line,s);std::exit(1);}
#define CHECK(c) do {if(!(c))fail(#c,__LINE__);++checks;}while(0)
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {
    CHECK(a->x1>=0 && a->y1>=0 && a->x2<240 && a->y2<240);
    for(int y=a->y1;y<=a->y2;++y)
        memcpy(pixels+(y*240+a->x1)*2,p+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);
    flush_pixels+=(a->x2-a->x1+1)*(a->y2-a->y1+1);lv_display_flush_ready(d);
}
static void read_touch(lv_indev_t *,lv_indev_data_t *d) {
    d->point={px,py};d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void submitted(void *,const char *pattern,size_t count) {
    CHECK(vault::pattern::valid(pattern,count));++submissions;
}
static lv_indev_t *input;
static lv_obj_t *parent;
static ui::VaultPattern view;
static void refresh_compare(const char *step) {
    const auto dirty=requested_dirty;requested_dirty.clear();
    discs.clear();flush_pixels=0;lv_refr_now(nullptr);partial_pixels+=flush_pixels;
    std::array<unsigned char,sizeof(pixels)> expected;
    memcpy(expected.data(),pixels,sizeof(pixels));
    lv_obj_invalidate(lv_screen_active());discs.clear();flush_pixels=0;lv_refr_now(nullptr);full_pixels+=flush_pixels;
    bool different=memcmp(expected.data(),pixels,sizeof(pixels))!=0;
    if(different) {
        // LVGL's rounded-cap antialiasing can round RGB565 by one channel
        // quantum where a render stripe clips the cap. Accept only differences
        // inside a requested dirty rectangle, <=1 quantum per channel, AND
        // exactly reproducible by repainting the same state with those clips.
        bool bounded=true;unsigned changed=0;
        for(unsigned i=0;i<sizeof(pixels);i+=2) {
            const unsigned a=expected[i]+256*expected[i+1],b=pixels[i]+256*pixels[i+1];
            if(a==b)continue;++changed;
            const int x=i/2%240,y=i/2/240;bool covered=false;
            for(const auto &r:dirty)if(x>=r.x1&&x<=r.x2&&y>=r.y1&&y<=r.y2)covered=true;
            bounded=bounded&&covered&&std::abs(int(a>>11)-int(b>>11))<=1&&
                std::abs(int((a>>5)&63)-int((b>>5)&63))<=1&&std::abs(int(a&31)-int(b&31))<=1;
        }
        if(bounded) {
            for(const auto &r:dirty)lv_obj_invalidate_area(lv_obj_get_child(parent,0),&r);
            lv_refr_now(nullptr);
            if(memcmp(expected.data(),pixels,sizeof(pixels))==0) {
                ++quantized_frames;quantized_pixels+=changed;different=false;
                lv_obj_invalidate(lv_screen_active());discs.clear();lv_refr_now(nullptr);
            }
        }
    }
    if(different) {
        unsigned mismatches=0;for(unsigned i=0;i<sizeof(pixels);i+=2)
            if(expected[i]!=pixels[i] || expected[i+1]!=pixels[i+1])++mismatches;
        fprintf(stderr,"frame %u (%s): %u pixels differ; touch (%d,%d)->(%d,%d)\n",frames,step,mismatches,old_px,old_py,px,py);
        for(unsigned i=0;i<sizeof(pixels);i+=2) if(expected[i]!=pixels[i] || expected[i+1]!=pixels[i+1])
            fprintf(stderr,"pixel (%u,%u): partial=%04x full=%04x\n",i/2%240,i/2/240,expected[i]+256*expected[i+1],pixels[i]+256*pixels[i+1]);
        const char *names[]={"failed-partial.ppm","failed-full.ppm"};
        const unsigned char *images[]={expected.data(),pixels};
        for(int image=0;image<2;++image){FILE *f=fopen(names[image],"wb");fprintf(f,"P6\n240 240\n255\n");for(unsigned i=0;i<240*240;++i){unsigned p=images[image][i*2]+256*images[image][i*2+1];unsigned char rgb[]={(unsigned char)(((p>>11)*255)/31),(unsigned char)((((p>>5)&63)*255)/63),(unsigned char)(((p&31)*255)/31)};fwrite(rgb,1,3,f);}fclose(f);}
        std::exit(1);
    }
    ++frames;++checks;
    // The observer sees the actual draw descriptors; repeated calls due to
    // partial draw-buffer stripes must all use the same 15px / 60px geometry.
    unsigned centers=0,halos=0;
    for(const auto &disc:discs) {
        const int w=disc.area.x2-disc.area.x1+1,h=disc.area.y2-disc.area.y1+1;
        CHECK(w==h && (w==15 || w==60));
        if(w==15)++centers;else {++halos;CHECK(disc.opacity>LV_OPA_TRANSP && disc.opacity<LV_OPA_COVER);}
    }
    CHECK(centers>=9 && centers==halos);
}
static void touch(int x,int y,bool down,const char *step) {
    old_px=px;old_py=py;px=x;py=y;pressed=down;lv_tick_inc(16);lv_indev_read(input);refresh_compare(step);
}
static void reset_view() {
    pressed=false;lv_indev_read(input);view.detach();lv_obj_clean(parent);
    view.create(parent,"",true,submitted,nullptr);lv_obj_update_layout(parent);refresh_compare("create");
}
static void capture(const char *path) {
    FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<240*240;++i){unsigned p=pixels[i*2]+256*pixels[i*2+1];unsigned char rgb[]={
        (unsigned char)(((p>>11)*255)/31),(unsigned char)((((p>>5)&63)*255)/63),(unsigned char)(((p&31)*255)/31)};fwrite(rgb,1,3,f);}fclose(f);
}
int main() {
    lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,buffer2,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    auto screen=lv_screen_active();lv_obj_remove_style_all(screen);lv_obj_set_style_bg_color(screen,lv_color_hex(0x101E1B),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    parent=lv_obj_create(screen);lv_obj_remove_style_all(parent);lv_obj_set_size(parent,240,240);lv_obj_set_scrollable(parent,false);
    input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,read_touch);
    reset_view();capture("idle.ppm");
    // Incremental tracing must use substantially less transfer than full frame.
    touch(48,48,true,"press");partial_pixels=full_pixels=0;
    for(int i=1;i<=30;++i)touch(48+i,48+i/3,true,"short incremental motion");
    const auto short_partial=partial_pixels,short_full=full_pixels;
    CHECK(short_partial<short_full/2);
    touch(192,48,true,"fast horizontal crossing");touch(192,192,true,"fast vertical crossing");
    touch(48,48,true,"retrace selected diagonal");touch(48,192,true,"new vertical crossing");capture("connected.ppm");
    touch(192,192,false,"release");CHECK(submissions==1);touch(192,192,false,"repeat release");CHECK(submissions==1);
    lv_tick_inc(451);view.poll();refresh_compare("clear after release");
    reset_view();touch(120,120,true,"short press");touch(120,120,false,"short invalid release");
    lv_tick_inc(1801);view.poll();refresh_compare("error toast expiry and clear");
    reset_view();touch(0,0,true,"begin outside nodes");touch(239,239,true,"screen diagonal");touch(239,0,true,"turn");touch(0,239,true,"reverse turn");touch(0,239,false,"release at edge");
    reset_view();touch(48,48,true,"cancel press");touch(192,48,true,"cancel motion");
    auto board=lv_obj_get_child(parent,0);lv_obj_send_event(board,LV_EVENT_PRESS_LOST,nullptr);refresh_compare("press lost");CHECK(!view.drawing());
    touch(192,192,false,"release after cancel");
    reset_view();touch(48,48,true,"reset press");lv_obj_send_event(lv_obj_get_child(parent,0),LV_EVENT_INDEV_RESET,nullptr);refresh_compare("input reset");CHECK(!view.drawing());
    touch(48,48,false,"reset release");
    std::mt19937 random(0x594D5041);
    for(unsigned trial=0;trial<18;++trial) {
        reset_view();touch(random()%240,random()%240,true,"random press");
        for(unsigned step=0;step<24;++step)touch(random()%240,random()%240,true,"random move");
        touch(random()%240,random()%240,false,"random release");lv_tick_inc(1801);view.poll();refresh_compare("random clear");
    }
    ui::theme::test_light=true;
    lv_obj_set_style_bg_color(screen,lv_color_hex(0xF6F0E7),0);
    reset_view();capture("idle-light.ppm");
    touch(48,48,true,"light press");touch(192,48,true,"light crossing");touch(192,192,true,"light vertical");
    touch(48,192,true,"light turn");touch(120,120,true,"light diagonal");capture("connected-light.ppm");
    touch(120,120,false,"light release");lv_tick_inc(1801);view.poll();refresh_compare("light clear");
    for(unsigned trial=0;trial<6;++trial) {
        reset_view();touch(random()%240,random()%240,true,"light random press");
        for(unsigned step=0;step<24;++step)touch(random()%240,random()%240,true,"light random move");
        touch(random()%240,random()%240,false,"light random release");lv_tick_inc(1801);view.poll();refresh_compare("light random clear");
    }
    view.detach();lv_indev_delete(input);lv_display_delete(display);lv_deinit();
    printf("{\"passed\":true,\"checks\":%u,\"frames\":%u,\"pixel_equal_frames\":%u,\"verified_clip_quantization_frames\":%u,\"verified_clip_quantization_pixels\":%u,\"center_diameter\":15,\"halo_diameter\":60,\"short_motion_partial_pixels\":%llu,\"short_motion_full_pixels\":%llu,\"submissions\":%u}\n",checks,frames,frames-quantized_frames,quantized_frames,quantized_pixels,short_partial,short_full,submissions);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--lvgl-root', type=Path, required=True, help='External pinned LVGL source directory')
    parser.add_argument('--reuse-c-from', type=Path, help='Reuse verified local LVGL objects from a previous successful replay')
    args = parser.parse_args()
    run = args.run_dir.resolve()
    assert run.is_relative_to((ROOT / 'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    production = run / 'production'
    production.mkdir()
    (run / 'core').mkdir()
    source = ROOT / 'firmware/main'
    tracked = ['ui/vault_pattern.cpp', 'ui/vault_pattern.hpp', 'core/vault_pattern.hpp', 'core/vault_format.hpp']
    hashes = {}
    for name in tracked:
        data = (source / name).read_bytes()
        hashes[name] = hashlib.sha256(data).hexdigest()
        target = production / Path(name).name if name.startswith('ui/') else run / name
        target.write_bytes(data)
    stubs = {
        'production/compact_ui.hpp': '#pragma once\n#include "lvgl.h"\nnamespace ui::compact { inline lv_obj_t *label(lv_obj_t *p,const char *s,int width) {auto o=lv_label_create(p);lv_obj_set_width(o,width);lv_label_set_text(o,s);return o;} inline void setText(lv_obj_t *o,const char *s) {lv_label_set_text(o,s);} }\n',
        'production/theme.hpp': '#pragma once\n#include "lvgl.h"\nnamespace ui::theme {inline bool test_light=false;inline lv_color_t bg(){return lv_color_hex(test_light?0xF6F0E7:0x101E1B);}inline lv_color_t accent(){return lv_color_hex(test_light?0xB95435:0xD5EF8D);}inline lv_color_t muted(){return lv_color_hex(test_light?0x75604D:0xA0B7A2);}}\n',
        'core/power_mgr.hpp': '#pragma once\nnamespace sys {class Power {public:static bool isScreenOn(){return true;}static bool sleepPending(){return false;}};}\n',
    }
    for name, data in stubs.items():
        (run / name).write_text(data, encoding='utf8')
    lv = args.lvgl_root.resolve()
    config = (lv / 'lv_conf_template.h').read_text(encoding='utf8').replace('#if 0 /* Set this to "1" to enable content */', '#if 1')
    config = re.sub(r'#define LV_MEM_SIZE [^\n]+', '#define LV_MEM_SIZE (4 * 1024 * 1024U)', config)
    (run / 'lv_conf.h').write_text(config, encoding='utf8')
    harness = run / 'pattern_runtime.cpp'
    harness.write_text(HARNESS, encoding='utf8')
    files = [p for p in (lv / 'src').rglob('*.c') if not set(p.relative_to(lv / 'src').parts) & {'libs', 'drivers'}]
    files += list((lv / 'src/libs/bin_decoder').glob('*.c'))
    exe = run / 'pattern_runtime.exe'
    common = ['-O1', '-I' + str(run), '-I' + str(lv), '-DLV_CONF_INCLUDE_SIMPLE']
    command = common + ['-c'] + list(map(str, files))
    rsp = run / 'compile.rsp'
    rsp.write_text('\n'.join('"' + v.replace('\\', '/') + '"' for v in command), encoding='utf8')
    stages = []
    object_dir = run
    if args.reuse_c_from:
        object_dir = args.reuse_c_from.resolve()
        assert object_dir.is_relative_to((ROOT / 'artifacts/validation').resolve())
        assert json.loads((object_dir / 'result.json').read_text())['passed']
        assert (object_dir / 'lv_conf.h').read_bytes() == (run / 'lv_conf.h').read_bytes()
        for file in files:
            obj = object_dir / (file.stem + '.o')
            assert obj.is_file() and obj.stat().st_mtime_ns >= file.stat().st_mtime_ns
        stages.append({'stage': 'reuse-local-c-objects', 'source': str(object_dir)})
    env = dict(os.environ)
    env['PATH'] = str(args.compiler.resolve().parent) + os.pathsep + env.get('PATH', '')
    link_rsp = run / 'link.rsp'
    link_rsp.write_text('\n'.join('"' + v.replace('\\', '/') + '"' for v in common + ['-o', str(exe), str(harness)] + [str(object_dir / (p.stem + '.o')) for p in files]), encoding='utf8')
    commands = [] if args.reuse_c_from else [('compile-c', [str(args.compiler.resolve().with_name('gcc.exe')), '@' + str(rsp)])]
    commands += [('compile-cpp', [str(args.compiler.resolve()), '@' + str(link_rsp)]), ('execute', [str(exe)])]
    for name, cmd in commands:
        result = subprocess.run(cmd, cwd=run, env=env, capture_output=True, timeout=240)
        (run / (name + '.stdout.log')).write_bytes(result.stdout)
        (run / (name + '.stderr.log')).write_bytes(result.stderr)
        stages.append({'stage': name, 'exit_code': result.returncode})
        if result.returncode:
            print(result.stderr.decode(errors='replace')[-12000:])
            raise RuntimeError(name + ' failed')
    replay = json.loads(result.stdout.decode().strip().splitlines()[-1])
    evidence = {'passed': replay['passed'], 'compiler': str(args.compiler.resolve()), 'stages': stages,
                'replay': replay, 'source_sha256': hashes, 'buffer_rows': 20, 'double_buffer': True, 'palettes': ['Terminal', 'Warm'],
                'method': 'Byte-identical production view and gesture source; real LVGL partial RGB565 render versus forced full refresh after every fake pointer event; only power/palette/label helpers stubbed.'}
    (run / 'result.json').write_text(json.dumps(evidence, indent=2), encoding='utf8')
    print(json.dumps(replay))


if __name__ == '__main__':
    main()
