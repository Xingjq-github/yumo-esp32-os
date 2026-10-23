"""Native garden model, real file stores and LVGL replay; never builds or flashes ESP32."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,zlib
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--run-dir',type=Path,required=True)
    ap.add_argument('--lvgl-root',type=Path,default=ROOT/'dependencies/managed_components/lvgl__lvgl',help='Pinned LVGL source directory, including external installations')
    ap.add_argument('--resume',action='store_true');ap.add_argument('--model-only',action='store_true');a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=a.resume)
    cc=a.compiler.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    main=ROOT/'firmware/main';lv=a.lvgl_root.resolve()
    inputs={}
    def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    def execute(stage,exe,args):
        rsp=run/(stage+'.rsp');rsp.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),encoding='utf8')
        proc=subprocess.run([str(exe),'@'+str(rsp)] if args else [str(exe)],cwd=run,env=env,capture_output=True,timeout=300)
        (run/(stage+'.log')).write_bytes(proc.stdout+proc.stderr)
        if proc.returncode:print((proc.stdout+proc.stderr).decode('utf8','replace'));raise RuntimeError(stage+' failed')
        return proc
    # Each invocation gets a separate directory; never remove or overwrite earlier replay stores.
    modeldir=run/'model-cases';modeldir.mkdir(exist_ok=a.resume)
    attempt=1
    while (modeldir/str(attempt)).exists():attempt+=1
    case=modeldir/str(attempt);case.mkdir()
    flags=['-O0','-g','-ffunction-sections','-fdata-sections','-DESTALE=116','-I'+str(main)]
    models=[main/'core/garden_model.cpp',main/'core/garden_store.cpp',ROOT/'tooling/firmware/garden_model_host.cpp']
    execute('model-compile',cc,flags+['-std=c++17']+models+['-o',run/'model.exe'])
    model=subprocess.run([str(run/'model.exe')],cwd=case,env=env,capture_output=True,timeout=90)
    (run/'model.log').write_bytes(model.stdout+model.stderr);print(model.stdout.decode('utf8','replace'))
    if model.returncode:print(model.stderr.decode('utf8','replace'));raise RuntimeError('model replay failed')
    inputs.update({(p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else '@lvgl/'+p.relative_to(lv).as_posix()):digest(p) for p in models})
    if not a.model_only:
        # Match the actual display's color format and compile production LVGL controls/fonts.
        config='''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (8*1024*1024U)
#define LV_USE_OS LV_OS_NONE
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
'''
        (run/'lv_conf.h').write_text(config,encoding='utf8')
        (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n')
        (run/'esp_random.h').write_text('#pragma once\n#include <cstdint>\nuint32_t esp_random();\n')
        (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0;\ninline const char *esp_err_to_name(int){return "host";}\n')
        stubs={
            'esp_timer.h':'#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
            'driver/gpio.h':'#pragma once\nenum gpio_num_t {GPIO_NUM_0=0,GPIO_NUM_4=4,GPIO_NUM_5=5};\ninline int gpio_get_level(gpio_num_t){return 1;}\n',
            'freertos/FreeRTOS.h':'''#pragma once
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) (++*(p))
#define portEXIT_CRITICAL(p) (--*(p))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
''',
            'freertos/task.h':'#pragma once\ninline int xTaskCreate(void (*)(void *),const char *,int,void *,int,void *){return 1;}\ninline void vTaskDelay(int){}\n',
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
        for name,source in stubs.items():
            p=run/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(source,encoding='utf8')
        buttons=main/'core/button_mgr.cpp';source=buttons.read_text(encoding='utf8')
        for header in ('settings_store.hpp','system_service.hpp','screenshot.hpp'):
            source=source.replace('#include "'+header+'"','#include "host_services.hpp"')
        (run/'production_buttons.cpp').write_text(source,encoding='utf8')
        flags+=['-DLV_CONF_INCLUDE_SIMPLE']+['-I'+str(p) for p in (run,lv,main/'core',main/'ui',main/'apps')]
        cfiles=[f for f in (lv/'src').rglob('*.c') if not set(f.relative_to(lv/'src').parts)&{'libs','drivers'}]
        cfiles+=list((lv/'src/libs/bin_decoder').glob('*.c'))
        cfiles+=[main/'ui/lv_font_cjk_16.c',main/'ui/lv_font_menu_14.c',main/'ui/lv_font_reading_15.c']
        cacheKey={'flags':flags,'inputs':{str(p):digest(p) for p in list(lv.rglob('*.h'))+cfiles+[run/'lv_conf.h',cc.with_name('gcc.exe')]}}
        cacheFile=run/'lvgl-cache.json';cache=json.loads(cacheFile.read_text()) if cacheFile.exists() else {}
        objects={p.name:digest(p) for p in run.glob('*.o')}
        if not(a.resume and objects and cache.get('key')==cacheKey and cache.get('objects')==objects):
            execute('lvgl',cc.with_name('gcc.exe'),flags+['-c']+cfiles)
            cacheFile.write_text(json.dumps({'key':cacheKey,'objects':{p.name:digest(p) for p in run.glob('*.o')}},indent=2))
        cpp=[main/'apps/garden.cpp',main/'core/garden_model.cpp',main/'ui/garden_scene.cpp',main/'ui/compact_ui.cpp',main/'ui/theme.cpp',ROOT/'tooling/firmware/garden_ui_host.cpp',run/'production_buttons.cpp']
        execute('ui-compile',cc,flags+['-std=c++17','-Wl,--gc-sections']+cpp+list(run.glob('*.o'))+['-o',run/'ui.exe'])
        result=execute('ui',run/'ui.exe',[]);print(result.stdout.decode('utf8','replace'))
        inputs.update({(p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else '@lvgl/'+p.relative_to(lv).as_posix()):digest(p) for p in cpp+cfiles+[buttons,main/'core/button_mgr.hpp'] if p!=run/'production_buttons.cpp'})
        for ppm in run.glob('*.ppm'):
            data=ppm.read_bytes().split(b'\n',3)[3];raw=b''.join(b'\0'+data[y*720:(y+1)*720] for y in range(240))
            def chunk(t,p):return struct.pack('>I',len(p))+t+p+struct.pack('>I',zlib.crc32(t+p))
            ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',240,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    for pattern in ('core/garden*.*','apps/garden*.*','ui/garden*.*'):
        for p in main.glob(pattern):inputs[(p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else '@lvgl/'+p.relative_to(lv).as_posix())]=digest(p)
    inputs[Path(__file__).relative_to(ROOT).as_posix()]=digest(Path(__file__))
    (run/'result.json').write_text(json.dumps({'passed':True,'model_only':a.model_only,'firmware_build':False,'flash':False,'inputs':inputs},indent=2))
if __name__=='__main__':main()
