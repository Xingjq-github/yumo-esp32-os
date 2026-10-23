"""Replay production app, compact widgets and fonts using native LVGL."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,zlib
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
 run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
 cc=a.compiler.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','');lv=ROOT/'dependencies/managed_components/lvgl__lvgl';main=ROOT/'firmware/main'
 (run/'lv_conf.h').write_text('''#ifndef LV_CONF_H
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
''')
 (run/'host_services.hpp').write_text('''#pragma once
#include <string>
namespace sys {
enum class WifiState {Off,Disconnected,Searching,Connecting,Connected,Failed};
struct WifiSnapshot {WifiState state=WifiState::Off;std::string ssid,ip;int rssi=0;};
class Connectivity {public:static WifiSnapshot snapshot();};
class Power {public:static bool isScreenOn();};
}
''')
 (run/'esp_log.h').write_text('#pragma once\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n')
 app=main/'apps/speed_test.cpp';source=app.read_text(encoding='utf8').replace('#include "core/connectivity.hpp"','#include "host_services.hpp"').replace('#include "core/power_mgr.hpp"','')
 (run/'production_app.cpp').write_text(source,encoding='utf8')
 cfiles=[f for f in (lv/'src').rglob('*.c') if not set(f.relative_to(lv/'src').parts)&{'libs','drivers'}]+list((lv/'src/libs/bin_decoder').glob('*.c'))
 cfiles += [main/'ui/lv_font_cjk_16.c',main/'ui/lv_font_menu_14.c',main/'ui/lv_font_reading_15.c']
 flags=['-O0','-g','-ffunction-sections','-fdata-sections','-DESTALE=116','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(run),'-I'+str(lv),'-I'+str(main),'-I'+str(main/'ui'),'-I'+str(main/'apps')]
 def execute(stage,exe,args):
  rsp=run/(stage+'.rsp');rsp.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),encoding='utf8')
  cmd=[str(exe),'@'+str(rsp)] if args else [str(exe)]
  proc=subprocess.run(cmd,cwd=run,env=env,capture_output=True,timeout=240);(run/(stage+'.log')).write_bytes(proc.stdout+proc.stderr)
  if proc.returncode:print((proc.stdout+proc.stderr).decode('utf8','replace'));raise RuntimeError(stage+' failed')
  return proc
 execute('lvgl',cc.with_name('gcc.exe'),flags+['-c']+cfiles)
 cpp=[run/'production_app.cpp',main/'ui/compact_ui.cpp',main/'ui/theme.cpp',ROOT/'tooling/firmware/speed_test_ui_host.cpp']
 execute('compile',cc,flags+['-std=c++17','-Wl,--gc-sections']+cpp+list(run.glob('*.o'))+['-o',str(run/'ui.exe')])
 proc=execute('execute',run/'ui.exe',[])
 for ppm in run.glob('*.ppm'):
  data=ppm.read_bytes().split(b'\n',3)[3];raw=b''.join(b'\0'+data[y*720:(y+1)*720] for y in range(240))
  def chunk(tag,payload):return struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',zlib.crc32(tag+payload))
  ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',240,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
 files=[app,main/'apps/speed_test.hpp',main/'ui/compact_ui.cpp',main/'ui/theme.cpp',main/'ui/lv_font_menu_14.c',main/'ui/lv_font_cjk_16.c',ROOT/'tooling/firmware/speed_test_ui_host.cpp']
 result={'passed':True,'method':'production app/UI/fonts with real LVGL; only network/job/system services replaced with host stubs','output':proc.stdout.decode('utf8','replace').strip(),'inputs':{f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in files}}
 (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(result['output'])
if __name__=='__main__':main()
