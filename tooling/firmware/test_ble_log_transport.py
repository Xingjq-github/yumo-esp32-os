"""Replay real NimBLE notification logging through ESP-IDF log filtering and USB TX."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = Path(__import__('os').environ['IDF_PATH']) / 'components'

def function(text, name):
    match = re.search(r'\b'+re.escape(name)+r'\([^;{}]*\)\s*\{', text)
    if not match: raise ValueError('definition not found: '+name)
    start = match.start()
    start = text.rfind('\n', 0, start) + 1
    brace = text.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir', type=Path, required=True)
    args = ap.parse_args()
    out = args.run_dir.resolve()
    if not out.is_relative_to((ROOT/'artifacts/validation').resolve()):
        raise ValueError('run-dir must be under artifacts/validation')
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT/'firmware/main/core/ble_hid.cpp'
    gatt = SDK/'bt/host/nimble/nimble/nimble/host/src/ble_gattc.c'
    usb = SDK/'esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c'
    sdkconfig = ROOT/'firmware/sdkconfig'
    config = sdkconfig.read_text(encoding='utf-8')
    defines = []
    for name, value in re.findall(r'^(CONFIG_(?:LOG_[A-Z0-9_]+|BT_NIMBLE_LOG_LEVEL))=(.+)$', config, re.M):
        defines.append(f'#define {name} {1 if value=="y" else value}\n')
    (out/'sdkconfig.h').write_text(''.join(defines), encoding='utf-8')
    (out/'esp_rom_sys.h').write_text('#pragma once\n#include <stdarg.h>\nint esp_rom_printf(const char*, ...);\n',encoding='utf-8')
    (out/'esp_attr.h').write_text('#pragma once\n#define IRAM_ATTR\n#define DRAM_ATTR\n#define DRAM_STR(s) (s)\n#define NOLOAD_ATTR\n',encoding='utf-8')
    target_cc=Path(__import__('shutil').which('xtensa-esp-elf-gcc') or 'xtensa-esp-elf-gcc').resolve()
    sysroot=Path(subprocess.check_output([str(target_cc),'-print-sysroot'],text=True).strip()).resolve()
    queue_header=next((sysroot/path for path in ('sys-include/sys/queue.h','include/sys/queue.h')
                       if (sysroot/path).is_file()),None)
    if queue_header is None:
        raise RuntimeError('The activated target toolchain does not provide sys/queue.h')
    (out/'sys').mkdir()
    (out/'sys/queue.h').write_bytes(queue_header.read_bytes())
    code = gatt.read_text(encoding='utf-8')
    extracted = '\n'.join('static void\n'+function(code, n) for n in ('ble_gattc_log_proc_init','ble_gattc_log_notify'))
    (out/'notify_log.inc').write_text(extracted,encoding='utf-8')
    usbtext = usb.read_text(encoding='utf-8')
    timeout = re.search(r'^#define TX_FLUSH_TIMEOUT_US .+$',usbtext,re.M).group()
    (out/'usb_tx.inc').write_text(timeout+'\n'+function(usbtext,'usb_serial_jtag_tx_char_no_driver'),encoding='utf-8')
    text = source.read_text(encoding='utf-8')
    init = text[text.index('esp_err_t BleHid::init(void)'):text.index('esp_err_t BleHid::setEnabled(')]
    setting = re.findall(r'esp_log_level_set\("NimBLE",\s*ESP_LOG_WARN\);',init)
    assert len(setting)==1 and init.index(setting[0])<init.index('nimble_port_init()')
    (out/'policy.inc').write_text(setting[0],encoding='utf-8')
    nimble = SDK/'bt/host/nimble/nimble/porting/nimble/include'
    log = SDK/'log'
    units = [
        log/'src/log.c', log/'src/os/log_write.c', log/'src/log_level/log_level.c',
        log/'src/log_level/tag_log_level/tag_log_level.c',
        log/'src/log_level/tag_log_level/linked_list/log_linked_list.c',
        log/'src/log_level/tag_log_level/cache/log_binary_heap.c',
        ROOT/'tooling/firmware/native/ble_log_transport_replay.c',
    ]
    cc = Path(__import__('shutil').which('gcc') or 'gcc').resolve()
    env = dict(os.environ); env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    includes = [out, log/'include', SDK/'esp_common/include',nimble]
    exe = out/'replay.exe'
    cmd = [str(cc),'-std=gnu17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DESP_PLATFORM',
           '-D__unused=__attribute__((unused))', *['-I'+str(p) for p in includes],
           *[str(p) for p in units],'-o',str(exe)]
    p = subprocess.run(cmd,capture_output=True,env=env,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (out/'compile.log').write_bytes(p.stdout+p.stderr)
    if p.returncode: raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-8000:])
    p = subprocess.run([str(exe)],capture_output=True,env=env,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (out/'replay.log').write_bytes(p.stdout+p.stderr)
    if p.returncode: raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-8000:])
    result = json.loads(p.stdout)
    paths = [source,gatt,usb,sdkconfig,queue_header,*units,Path(__file__).resolve()]
    paths += list((log/'include').rglob('*.h'))
    paths += [nimble/'modlog/modlog.h',nimble/'logcfg/logcfg.h',nimble/'log_common/log_common.h']
    result.update(inputs={(str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else '@sdk/'+p.relative_to(Path(__import__('os').environ['IDF_PATH'])).as_posix() if p.is_relative_to(Path(__import__('os').environ['IDF_PATH'])) else '@toolchain/'+p.name):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
        real_sdk_log_library=True, real_nimble_log_macros=True, real_usb_tx_function=True,
        actual_log_configuration=True, device_accessed=False,
        limits='Real SDK call sites, log library/tag linked list/cache and USB TX loop; only RTOS locks, clock and USB FIFO are simulated. Simulated stall durations are not measured radio latency.')
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(p.stdout.decode())

if __name__=='__main__': main()
