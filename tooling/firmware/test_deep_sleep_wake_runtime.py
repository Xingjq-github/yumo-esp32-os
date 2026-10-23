"""Run the production RTC wake stub against a GPIO/retained-watchdog simulator."""
from pathlib import Path
import argparse, hashlib, json, subprocess

ROOT = (Path(__file__).resolve().parents[2]/'firmware')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--run-dir',type=Path,required=True)
    args=p.parse_args();run=args.run_dir.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    (run/'fake_sdk.h').write_text('''#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __TINYC__
#define _Static_assert(test,message) typedef char rtc_channel_check[(test)?1:-1]
#endif
#define RTC_DATA_ATTR
#define RTC_IRAM_ATTR
#define CONFIG_IDF_TARGET_ESP32S3 1
#define RTCIO_GPIO5_CHANNEL 5
#define RTC_GPIO_IN_REG 1u
#define RTC_GPIO_IN_NEXT_S 10
#define RTC_CNTL_EXT_WAKEUP_CONF_REG 2u
#define RTC_CNTL_EXT_WAKEUP0_LV (1u<<30)
#define REG_READ(reg) mock_read(reg)
#define REG_SET_BIT(reg,bit) mock_level(reg,bit,true)
#define REG_CLR_BIT(reg,bit) mock_level(reg,bit,false)
typedef unsigned rtc_cntl_dev_t;
typedef void (*esp_deep_sleep_wake_stub_fn_t)(void);
extern rtc_cntl_dev_t RTCCNTL;
uint32_t mock_read(unsigned);
void mock_level(unsigned,unsigned,bool);
void rwdt_ll_write_protect_disable(rtc_cntl_dev_t *);
void rwdt_ll_feed(rtc_cntl_dev_t *);
void rwdt_ll_write_protect_enable(rtc_cntl_dev_t *);
void esp_default_wake_deep_sleep(void);
void esp_rom_delay_us(unsigned);
void esp_set_deep_sleep_wake_stub(esp_deep_sleep_wake_stub_fn_t);
void esp_wake_stub_sleep(esp_deep_sleep_wake_stub_fn_t);
''',encoding='utf8')
    for name in ('esp_attr.h','esp_sleep.h','esp_wake_stub.h','esp_rom_sys.h',
                 'hal/rwdt_ll.h','soc/rtc_io_reg.h','soc/rtc_cntl_reg.h',
                 'soc/rtc_io_channel.h','soc/soc.h','sdkconfig.h'):
        dest=run/name;dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_text('#include "fake_sdk.h"\n',encoding='utf8')
    exe=run/'wake_stub.exe'
    cmd=[str(args.compiler.resolve()),'-std=c11','-I'+str(run),'-I'+str(ROOT/'main'),
         str(ROOT/'../tooling/firmware/native/deep_sleep_wake_runtime.c'),'-o',str(exe)]
    compiled=subprocess.run(cmd,cwd=run,capture_output=True,text=True)
    (run/'compile.stdout.log').write_text(compiled.stdout,encoding='utf8')
    (run/'compile.stderr.log').write_text(compiled.stderr,encoding='utf8')
    if compiled.returncode:print(compiled.stderr,end='')
    compiled.check_returncode()
    result=subprocess.run([str(exe)],cwd=run,capture_output=True,text=True)
    (run/'stdout.log').write_text(result.stdout,encoding='utf8')
    (run/'stderr.log').write_text(result.stderr,encoding='utf8')
    inputs=('main/core/deep_sleep_wake.c','main/core/wake_click.h',
            '../tooling/firmware/native/deep_sleep_wake_runtime.c','../tooling/firmware/test_deep_sleep_wake_runtime.py')
    report={'passed':result.returncode==0,'exit_code':result.returncode,
            'method':'Production wake stub C with simulated GPIO/ROM sleep and retained 1 s RTC WDT; not a physical test',
            'result':result.stdout.strip(),'inputs':{
                name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs}}
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(result.stdout or result.stderr,end='')
    result.check_returncode()

if __name__=='__main__':main()
