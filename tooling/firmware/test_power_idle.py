"""Host power regression: actual supply and touch code, fake hardware, no firmware build."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    def execute(name,args):
        result=subprocess.run([str(v) for v in args],cwd=run,env=env,capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-7000:])
        return result.stdout.decode('utf-8')
    inputs=[]
    def read(name):
        path=ROOT/name;inputs.append(path);return path.read_text(encoding='utf-8')
    def fragment(name,target):
        text=read(name)
        # Keep production function bodies verbatim; only replace platform headers.
        (run/target).write_text(re.sub(r'^#include [<"].*\n','',text,flags=re.M),encoding='utf-8')
    fragment('main/core/battery_mgr.cpp','battery.inc')
    fragment('components/esp32_s3_touch_lcd_1_54/lvgl_power_adapter.c','adapter.inc')
    fragment('components/esp32_s3_touch_lcd_1_54/lvgl_power_touch.c','touch.inc')
    (run/'upstream_adapter.inc').write_text('static struct {void *task;bool inited,paused,pause_ack;void *pause_done_sem;} s_ctx={(void*)1,true,false,false,(void*)2};\n',encoding='utf-8')
    upstream=read('../dependencies/managed_components/espressif__esp_lvgl_adapter/src/input/esp_lv_adapter_input_touch.c')
    context=re.search(r'typedef struct \{\s*esp_lcd_touch_handle_t handle;.*?\} esp_lv_adapter_touch_ctx_t;',upstream,re.S).group(0)
    context=context.replace('esp_lv_adapter_touch_isr_ctx_t *isr_ctx','void *isr_ctx')
    start=upstream.index('static void lvgl_touch_read(lv_indev_t *indev_drv, lv_indev_data_t *data)\n{')
    end=upstream.index('\n}\n',start)+3
    (run/'upstream_touch.inc').write_text(context+'\n'+upstream[start:end],encoding='utf-8')
    bsp=read('components/esp32_s3_touch_lcd_1_54/esp32_s3_touch_lcd_1_54.c')
    (run/'touch_irq.inc').write_text(bsp[bsp.index('static esp_lcd_touch_interrupt_callback_t power_touch_original_irq;'):bsp.index('static esp_err_t power_touch_after_sleep')],encoding='utf-8')
    flags=['-I'+str(ROOT/'main'),'-I'+str(run)]
    execute('supply-compile',[cc,'-std=c++17','-O1','-Wall','-Wextra',*flags,ROOT/'../tooling/firmware/native/power_supply_test.cpp','-o','supply.exe','-static-libgcc','-static-libstdc++'])
    supply=execute('supply',[run/'supply.exe'])
    execute('touch-compile',[cc.with_name('gcc.exe'),'-std=c11','-O1','-Wall','-Wextra','-Wno-unused-function',*flags,
        '-DYUMO_LV_ADAPTER_SOURCE="upstream_adapter.inc"','-DYUMO_LV_TOUCH_SOURCE="upstream_touch.inc"',
        ROOT/'../tooling/firmware/native/power_touch_test.c','-o','touch.exe'])
    touch=execute('touch',[run/'touch.exe'])
    fragment('main/core/power_runtime.cpp','power_runtime.inc')
    (run/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;constexpr int ESP_OK=0;\n',encoding='utf-8')
    execute('deadline-compile',[cc,'-std=c++17','-O1','-Wall','-Wextra',*flags,
        ROOT/'../tooling/firmware/native/power_deadline_replay.cpp','-o','deadline.exe','-static-libgcc','-static-libstdc++'])
    deadline=execute('deadline',[run/'deadline.exe'])
    for name in ('power_policy_test','supply_policy_test'):
        inputs.append(ROOT/'../tooling/firmware'/(name+'.cpp'))
        execute(name,[cc,'-std=c++17','-Wall','-Wextra','-c',inputs[-1],'-o',name+'.o'])
    for name in ('main/core/battery_mgr.hpp','main/core/supply_policy.hpp','main/core/power_policy.hpp',
                 '../tooling/firmware/native/power_supply_test.cpp','../tooling/firmware/native/power_touch_test.c','../tooling/firmware/native/power_deadline_replay.cpp',
                 'main/core/power_mgr.hpp','../tooling/firmware/test_power_idle.py'):
        inputs.append(ROOT/name)
    result=dict(passed=True,supply=supply.strip(),touch=touch.strip(),deadline=deadline.strip(),policy_assertions=True,
                firmware_built=False,device_accessed=False,
                inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(supply+touch+deadline)
if __name__=='__main__':main()
