"""Compile and replay the production drift model and RTC/SNTP adapter without a device."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir',type=Path,required=True)
    run=parser.parse_args().run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    stubs={
        'esp_err.h':'#pragma once\nusing esp_err_t=int;\n',
        'esp_attr.h':'#pragma once\n#define IRAM_ATTR\n#define RTC_DATA_ATTR\n#define DRAM_ATTR\n',
        'esp_log.h':'#pragma once\n#define ESP_LOGI(...)\n',
        'esp_system.h':'#pragma once\nconstexpr int ESP_RST_DEEPSLEEP=5;int esp_reset_reason();\n',
        'esp_sleep.h':'#pragma once\nconstexpr int ESP_SLEEP_WAKEUP_EXT0=2;int esp_sleep_get_wakeup_cause();\n',
        'esp_private/esp_clk.h':'#pragma once\n#include <cstdint>\nuint32_t esp_clk_slowclk_cal_get();\n',
        'soc/rtc.h':'#pragma once\n#include <cstdint>\nenum soc_clk_freq_calculation_src_t {CLK_CAL_RTC_SLOW,CLK_CAL_OTHER};\nuint64_t rtc_time_get();uint64_t rtc_time_slowclk_to_us(uint64_t,uint32_t);\n',
        'sys/time.h':'#pragma once\n#include <ctime>\n#define timeval FakeTimeval\nusing suseconds_t=long;\nstruct timeval {time_t tv_sec;suseconds_t tv_usec;};\nint gettimeofday(timeval*,void*);int settimeofday(const timeval*,const void*);\n',
    }
    for name,text in stubs.items():
        p=run/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
    cc=Path(shutil.which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    outputs={}
    for name in ('clock_drift','clock_discipline'):
        source=ROOT/'tooling/firmware/native'/f'{name}_replay.cpp'
        exe=run/f'{name}.exe'
        cmd=[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-I'+str(run),'-I'+str(ROOT/'firmware/main'),str(source),'-o',str(exe),'-static-libgcc','-static-libstdc++']
        flags=dict(cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        p=subprocess.run(cmd,**flags);(run/f'{name}-compile.log').write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
        p=subprocess.run([str(exe)],**flags);(run/f'{name}.log').write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
        outputs[name]=p.stdout.decode().strip()
    names=['firmware/main/core/clock_drift_model.hpp','firmware/main/core/clock_discipline.hpp','firmware/main/core/clock_discipline.cpp',
           'tooling/firmware/native/clock_drift_replay.cpp','tooling/firmware/native/clock_discipline_replay.cpp','tooling/firmware/test_clock_drift_runtime.py']
    report=dict(passed=True,outputs=outputs,device_accessed=False,long_sleep_measured=False,
                inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names})
    (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
