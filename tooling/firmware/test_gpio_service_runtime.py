"""Exercise the production GPIO installation adapter; no board access."""
from pathlib import Path
import argparse, json, os, subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',required=True,type=Path)
    run=ap.parse_args().run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False);(run/'driver').mkdir()
    (run/'driver/gpio.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=1,ESP_ERR_INVALID_STATE=2,ESP_ERR_NOT_SUPPORTED=3;\n')
    (run/'esp_intr_alloc.h').write_text('#pragma once\nconstexpr int ESP_INTR_FLAG_IRAM=1<<10,ESP_INTR_FLAG_LOWMED=14;\n')
    (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGE(...)\n')
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    commands=[('compile',[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-pthread','-static-libgcc','-static-libstdc++',
        '-I'+str(run),str(ROOT/'main/core/gpio_isr_service.cpp'),str(ROOT/'../tooling/firmware/native/gpio_service_replay.cpp'),'-o',str(run/'replay.exe')]),
        ('replay',[str(run/'replay.exe')])]
    for name,command in commands:
        p=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
    result=dict(passed=True,result=p.stdout.decode().strip(),device_accessed=False,interrupt_hardware_simulated=True)
    (run/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))

if __name__=='__main__':main()
