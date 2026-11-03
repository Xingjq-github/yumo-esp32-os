"""Replay the production Xiaozhi App and GPIO routing with real LVGL.

Network/audio, platform services and navigation are doubles. This does not
connect a device or the public Xiaozhi service. Reuse only audited LVGL objects.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,zlib

ROOT=Path(__file__).resolve().parents[2]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--run-dir',type=Path,required=True)
    ap.add_argument('--c-cache',type=Path,required=True)
    ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--resume',action='store_true',help='Reuse the same validation directory after rechecking cache hashes')
    a=ap.parse_args();run=a.run_dir.resolve();cache=a.c_cache.resolve()
    assert run.is_relative_to(ROOT/'artifacts/validation')
    assert cache.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True,exist_ok=a.resume)
    cc=a.compiler.resolve();fw=ROOT/'firmware';main=fw/'main'
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    manifest=json.loads((cache/'c-objects.json').read_text())
    changed=[];objects=[];inputs={}
    for n,digest in manifest['inputs'].items():
        path=cache/n if n=='lv_conf.h' else fw/n
        if sha(path)!=digest:
            assert path.resolve() in ((main/'ui/lv_font_menu_14.c').resolve(),(main/'ui/lv_font_app_icons.c').resolve()),n
            changed.append(path)
        inputs[str(path.resolve())]=sha(path)
    for n,digest in manifest['objects'].items():
        assert sha(cache/n)==digest,n
        if not (run/n).exists() or sha(run/n)!=digest:shutil.copyfile(cache/n,run/n)
        objects.append(run/n)
    shutil.copyfile(cache/'lv_conf.h',run/'lv_conf.h')
    stubs={
      'esp_err.h':'#pragma once\nusing esp_err_t=int; constexpr int ESP_OK=0;\ninline const char *esp_err_to_name(int){return "host";}\n',
      'esp_log.h':'#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n',
      'esp_timer.h':'#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
      'driver/gpio.h':'#pragma once\nenum gpio_num_t {GPIO_NUM_0=0,GPIO_NUM_4=4,GPIO_NUM_5=5};\ninline int gpio_get_level(gpio_num_t){return 1;}\n',
      'freertos/FreeRTOS.h':'#pragma once\nusing portMUX_TYPE=int;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) (++*(p))\n#define portEXIT_CRITICAL(p) (--*(p))\n#define pdMS_TO_TICKS(x) (x)\n#define pdPASS 1\n',
      'freertos/task.h':'#pragma once\ninline int xTaskCreate(void (*)(void *),const char *,int,void *,int,void *){return 1;}\ninline void vTaskDelay(int){}\n',
      'iot_button.h':'''#pragma once
#include "esp_err.h"
enum button_event_t {BUTTON_PRESS_DOWN,BUTTON_PRESS_UP,BUTTON_PRESS_END,BUTTON_SINGLE_CLICK,BUTTON_DOUBLE_CLICK,BUTTON_LONG_PRESS_START,EVENT_COUNT};
using button_cb_t=void (*)(void *,void *);
struct HostButton {button_event_t event;button_cb_t cb[EVENT_COUNT]{};};
using button_handle_t=HostButton *;struct button_config_t {};
inline button_event_t iot_button_get_event(button_handle_t b){return b->event;}
inline int iot_button_register_cb(button_handle_t b,button_event_t e,void *,button_cb_t f,void *){b->cb[e]=f;return 0;}
extern HostButton buttons[6];
''',
      'button_gpio.h':'''#pragma once
#include "iot_button.h"
struct button_gpio_config_t {int gpio_num=0,active_level=0;bool enable_power_save=false;};
inline int iot_button_new_gpio_device(button_config_t *,button_gpio_config_t *c,button_handle_t *out){*out=&buttons[c->gpio_num];return 0;}
''',
      'host_services.hpp':'''#pragma once
namespace sys {
class Settings {public:static int volume();};
class SystemService {public:static void setVolume(int);};
class Screenshot {public:static void request();};
}
'''}
    for name,text in stubs.items():
        p=run/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
    buttons=main/'core/button_mgr.cpp';source=buttons.read_text(encoding='utf-8')
    for header in ('settings_store.hpp','system_service.hpp','screenshot.hpp'):
        source=source.replace('#include "'+header+'"','#include "host_services.hpp"')
    (run/'production_buttons.cpp').write_text(source,encoding='utf-8')
    def execute(name,exe,args):
        rsp=run/(name+'.rsp');rsp.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),encoding='utf-8')
        proc=subprocess.run([str(exe),'@'+str(rsp)] if args else [str(exe)],cwd=run,env=env,capture_output=True,timeout=180,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(proc.stdout+proc.stderr)
        if proc.returncode:raise RuntimeError((proc.stdout+proc.stderr).decode('utf-8','replace')[-8000:])
        return proc.stdout.decode('utf-8','replace')
    # The audited LVGL cache predates the 32px activation code. Font switches
    # only add declarations/data; rebuild this production font with the switch
    # enabled, preserving every other cache input and its verified hash.
    extra_defines=['LV_FONT_MONTSERRAT_32=1']
    flags=['-O1','-DLV_CONF_INCLUDE_SIMPLE']+['-D'+d for d in extra_defines]+['-I'+str(run),'-I'+str(ROOT/'dependencies/managed_components/lvgl__lvgl'),'-I'+str(main),'-I'+str(main/'ui'),'-I'+str(main/'core')]
    icon=main/'ui/lv_font_app_icons.c'
    if not any((fw/n).resolve()==icon.resolve() for n in manifest['inputs']):
        changed.append(icon);objects.append(run/'lv_font_app_icons.o')
    font32=ROOT/'dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_32.c'
    changed.append(font32);inputs[str(font32.resolve())]=sha(font32)
    if run/'lv_font_montserrat_32.o' not in objects:objects.append(run/'lv_font_montserrat_32.o')
    if changed:execute('updated-font',cc.with_name('gcc.exe'),flags+['-std=c11','-c']+changed)
    cpp=[main/'apps/xiaozhi.cpp',main/'ui/compact_ui.cpp',main/'ui/theme.cpp',run/'production_buttons.cpp',ROOT/'tooling/firmware/native/xiaozhi_ui_replay.cpp']
    execute('compile',cc,flags+['-std=c++17','-ffunction-sections','-fdata-sections','-c']+cpp)
    execute('link',cc,['-static-libgcc','-static-libstdc++','-Wl,--gc-sections','-o',run/'ui.exe']+objects+[run/(p.stem+'.o') for p in cpp])
    output=execute('run',run/'ui.exe',[])
    for ppm in run.glob('*.ppm'):
        pixels=ppm.read_bytes().split(b'\n',3)[3]
        raw=b''.join(b'\0'+pixels[y*720:(y+1)*720] for y in range(240))
        def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
        ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',240,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    paths=[p for p in cpp if p!=run/'production_buttons.cpp']+[buttons,main/'apps/xiaozhi.hpp',main/'core/xiaozhi_service.hpp',main/'ui/app_icons.hpp',Path(__file__).resolve()]
    result={'passed':True,'output':output,'production_inputs':{p.relative_to(ROOT).as_posix():sha(p) for p in paths},'lvgl_inputs':inputs,'extra_compile_defines':extra_defines,'cache_manifest_sha256':sha(cache/'c-objects.json'),'physical_touch_tested':False,'service_tested':False,'flash':False}
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(output)
if __name__=='__main__':main()
