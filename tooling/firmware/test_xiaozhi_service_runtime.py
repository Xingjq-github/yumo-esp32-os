"""Run production Xiaozhi service and JSON protocol with synthetic transport/audio.

The replay exercises Session itself; only external I/O, task scheduling, clocks,
heap allocation and other services are doubles. No device or public service I/O.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True)
    a=ap.parse_args();run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=True)
    main=ROOT/'firmware/main';cc=a.compiler.resolve();env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    source=(main/'core/xiaozhi_service.cpp').read_text(encoding='utf-8')
    replaced=('audio_recorder.hpp','mp3_player.hpp','chiptune_player.hpp','game_audio.hpp','fc_emulator.hpp','connectivity.hpp')
    for name in replaced:source=source.replace('#include "'+name+'"','#include "host_services.hpp"')
    (run/'service.inc').write_text(source,encoding='utf-8')
    stubs={
      'esp_err.h':'#pragma once\nusing esp_err_t=int;constexpr int ESP_OK=0;\n',
      'esp_timer.h':'#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
      'esp_log.h':'#pragma once\ntemplate<typename... Args> void xiaozhi_host_log(const char *,const char *,Args...){}\n#define ESP_LOGI(...) xiaozhi_host_log(__VA_ARGS__)\n',
      'esp_heap_caps.h':'#pragma once\n#include <cstddef>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,unsigned);\nvoid heap_caps_free(void *);\n',
      'freertos/FreeRTOS.h':'#pragma once\n#define pdPASS 1\n',
      'freertos/task.h':'#pragma once\ninline unsigned uxTaskGetStackHighWaterMark(void *){return 32*1024;}\n',
      'freertos/idf_additions.h':'#pragma once\n#include <cstdint>\nint xTaskCreateWithCaps(void (*)(void *),const char *,unsigned,void *,unsigned,void *,unsigned);\nvoid vTaskDeleteWithCaps(void *);\n',
      'host_services.hpp':'''#pragma once
namespace core::recorder {bool idle();}
namespace core::mp3 {bool idle();}
namespace core::chip {bool idle();}
namespace core::game_audio {bool idle();}
namespace core::fc {bool idle();}
namespace sys {
enum class WifiState {Off,Disconnected,Searching,Connecting,Connected,Failed};
class Connectivity {public:static WifiState wifiState();};
}
'''}
    for name,text in stubs.items():
        path=run/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,encoding='utf-8')
    cjson=ROOT/'dependencies/managed_components/espressif__cjson/cJSON'
    paths=[ROOT/'tooling/firmware/native/xiaozhi_service_replay.cpp',main/'core/xiaozhi_protocol.cpp',cjson/'cJSON.c']
    files=paths+[main/'core/xiaozhi_service.cpp',main/'core/xiaozhi_service.hpp',main/'core/xiaozhi_protocol.hpp',main/'core/xiaozhi_audio.hpp',main/'core/xiaozhi_transport.hpp',main/'core/xiaozhi_cloud.hpp',main/'core/worker_wake.hpp',cjson/'cJSON.h',Path(__file__).resolve()]
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    command=[str(cc),'-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(run),'-I'+str(main),'-I'+str(main/'core'),'-I'+str(cjson)]+list(map(str,paths))+['-static-libgcc','-static-libstdc++','-o',str(run/'service.exe')]
    proc=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'compile.log').write_bytes(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError((proc.stdout+proc.stderr).decode('utf-8','replace'))
    proc=subprocess.run([str(run/'service.exe')],cwd=run,env=env,capture_output=True,timeout=60,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'run.log').write_bytes(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError((proc.stdout+proc.stderr).decode('utf-8','replace'))
    output=proc.stdout.decode('utf-8')
    for name,digest in hashes.items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,'Input changed during replay: '+name
    result={'passed':True,'output':output,'inputs':hashes,'production_include_replacements':list(replaced),'device_tested':False,'cloud_tested':False}
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(output)
if __name__=='__main__':main()
