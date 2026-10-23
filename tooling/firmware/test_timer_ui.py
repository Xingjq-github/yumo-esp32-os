"""Native replay of the production stopwatch, button callbacks, power policy and LVGL.

Only board/RTOS/system side effects and navigation are stubbed. No ESP32 build or flash.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,zlib
ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--run-dir',type=Path,required=True)
    ap.add_argument('--lvgl-root',type=Path,default=ROOT/'dependencies/managed_components/lvgl__lvgl',help='Pinned LVGL source directory, including external installations')
    ap.add_argument('--resume',action='store_true',help='Reuse only hash-verified native LVGL objects')
    a=ap.parse_args();run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True,exist_ok=a.resume)
    cc=a.compiler.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    lv=a.lvgl_root.resolve();main=ROOT/'firmware/main'
    files={
    'lv_conf.h':'''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (8*1024*1024U)
#define LV_USE_OS LV_OS_NONE
#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_ASSERT_OBJ 1
#define LV_USE_ASSERT_STYLE 1
#define LV_ASSERT_HANDLER {abort();}
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#endif
''',
    'esp_err.h':'''#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0;
inline const char *esp_err_to_name(int){return "host";}
''',
    'esp_log.h':'''#pragma once
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
''',
    'esp_timer.h':'''#pragma once
#include <cstdint>
int64_t esp_timer_get_time();
''',
    'driver/gpio.h':'''#pragma once
enum gpio_num_t {GPIO_NUM_0=0,GPIO_NUM_4=4,GPIO_NUM_5=5};
inline int gpio_get_level(gpio_num_t){return 1;}
''',
    'freertos/FreeRTOS.h':'''#pragma once
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) (++*(p))
#define portEXIT_CRITICAL(p) (--*(p))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
''',
    'freertos/task.h':'''#pragma once
inline int xTaskCreate(void (*)(void *),const char *,int,void *,int,void *){return 1;}
inline void vTaskDelay(int){}
''',
    'iot_button.h':'''#pragma once
#include "esp_err.h"
enum button_event_t {BUTTON_PRESS_DOWN,BUTTON_PRESS_UP,BUTTON_PRESS_END,BUTTON_SINGLE_CLICK,BUTTON_DOUBLE_CLICK,BUTTON_LONG_PRESS_START,EVENT_COUNT};
using button_cb_t=void (*)(void *,void *);
struct HostButton {button_event_t event;button_cb_t cb[EVENT_COUNT]{};};
using button_handle_t=HostButton *;
struct button_config_t {};
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
    for name,source in files.items():
        p=run/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(source,encoding='utf8')
    buttons=main/'core/button_mgr.cpp';source=buttons.read_text(encoding='utf8')
    for header in ('settings_store.hpp','system_service.hpp','screenshot.hpp'):
        source=source.replace('#include "'+header+'"','#include "host_services.hpp"')
    (run/'production_buttons.cpp').write_text(source,encoding='utf8')
    cfiles=[f for f in (lv/'src').rglob('*.c') if not set(f.relative_to(lv/'src').parts)&{'libs','drivers'}]
    cfiles+=list((lv/'src/libs/bin_decoder').glob('*.c'))
    cfiles+=[main/'ui/lv_font_cjk_16.c',main/'ui/lv_font_menu_14.c',main/'ui/lv_font_reading_15.c']
    flags=['-O0','-g','-ffunction-sections','-fdata-sections','-DESTALE=116','-DLV_CONF_INCLUDE_SIMPLE']
    flags+=['-I'+str(p) for p in (run,lv,main,main/'core',main/'ui',main/'apps')]
    def execute(stage,exe,args):
        rsp=run/(stage+'.rsp');rsp.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),encoding='utf8')
        proc=subprocess.run([str(exe),'@'+str(rsp)] if args else [str(exe)],cwd=run,env=env,capture_output=True,timeout=240)
        (run/(stage+'.log')).write_bytes(proc.stdout+proc.stderr)
        if proc.returncode:
            print((proc.stdout+proc.stderr).decode('utf8','replace'));raise RuntimeError(stage+' failed')
        return proc
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    cachedInputs={str(p):digest(p) for p in list(lv.rglob('*.h'))+cfiles+[run/'lv_conf.h',cc.with_name('gcc.exe')]}
    cacheKey={'inputs':cachedInputs,'flags':flags}
    cacheFile=run/'lvgl-cache.json';cached=json.loads(cacheFile.read_text()) if cacheFile.exists() else {}
    objects={p.name:digest(p) for p in run.glob('*.o')}
    reused=a.resume and objects and cached.get('key')==cacheKey and cached.get('objects')==objects
    if not reused:
        execute('lvgl',cc.with_name('gcc.exe'),flags+['-c']+cfiles)
        cacheFile.write_text(json.dumps({'key':cacheKey,'objects':{p.name:digest(p) for p in run.glob('*.o')}},indent=2),encoding='utf8')
    cpp=[main/'apps/timer.cpp',run/'production_buttons.cpp',main/'ui/compact_ui.cpp',main/'ui/theme.cpp',ROOT/'tooling/firmware/timer_ui_host.cpp']
    execute('compile',cc,flags+['-std=c++17','-Wl,--gc-sections']+cpp+list(run.glob('*.o'))+['-o',str(run/'ui.exe')])
    proc=execute('execute',run/'ui.exe',[])
    for ppm in run.glob('*.ppm'):
        data=ppm.read_bytes().split(b'\n',3)[3];raw=b''.join(b'\0'+data[y*720:(y+1)*720] for y in range(240))
        def chunk(tag,payload):return struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',zlib.crc32(tag+payload))
        ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',240,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    inputs=[main/'apps/timer.hpp',main/'core/stopwatch_model.hpp',main/'core/timer_key_state.hpp',main/'core/button_mgr.hpp',main/'core/power_policy.hpp',buttons]+[p for p in cpp if p!=run/'production_buttons.cpp']+cfiles+[Path(__file__).resolve()]
    result={'passed':True,'method':__doc__,'output':proc.stdout.decode('utf8','replace').strip(),'firmware_build':False,'flash':False,'inputs':{(p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else '@lvgl/'+p.relative_to(lv).as_posix()):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(result['output'])
if __name__=='__main__':main()
