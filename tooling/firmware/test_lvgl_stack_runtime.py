"""Exercise the real LVGL thread entry/exit through the PSRAM adapter with fake RTOS allocation."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
BSP=ROOT/'components/esp32_s3_touch_lcd_1_54'
GUARDS=('SPIRAM_XIP_FROM_PSRAM','SPIRAM_FETCH_INSTRUCTIONS',
        'SPIRAM_RODATA','FREERTOS_TASK_CREATE_ALLOW_EXT_MEM')

def extract(source, declaration):
    start=source.index(declaration+'\n{')
    left=source.index('{',start);depth=0
    for end in range(left,len(source)):
        if source[end]=='{':depth+=1
        elif source[end]=='}':
            depth-=1
            if not depth:return source[start:end+1]
    raise ValueError('Missing function body: '+declaration)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--run-dir',type=Path,required=True)
    args=parser.parse_args();run=args.run_dir.resolve();compiler=args.compiler.resolve()
    assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False);(run/'freertos').mkdir()
    def write(name,text): (run/name).write_text(text,encoding='utf-8')
    write('sdkconfig.h',''.join(f'#ifndef CONFIG_{g}\n#define CONFIG_{g} 1\n#endif\n' for g in GUARDS))
    write('freertos/FreeRTOS.h','''#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t configSTACK_DEPTH_TYPE;
typedef uint8_t StackType_t;
typedef void (*TaskFunction_t)(void *);
typedef struct FakeTask *TaskHandle_t;
#define pdPASS 1
#define pdFAIL 0
#define tskIDLE_PRIORITY 0
''')
    write('freertos/task.h','''#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreate(TaskFunction_t,const char *,configSTACK_DEPTH_TYPE,void *,UBaseType_t,TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
''')
    write('freertos/idf_additions.h','''#pragma once
#include "task.h"
BaseType_t xTaskCreateWithCaps(TaskFunction_t,const char *,configSTACK_DEPTH_TYPE,void *,UBaseType_t,TaskHandle_t *,UBaseType_t);
void vTaskDeleteWithCaps(TaskHandle_t);
''')
    write('esp_heap_caps.h','#pragma once\n#define MALLOC_CAP_SPIRAM 4\n#define MALLOC_CAP_8BIT 2\n')
    write('lv_test.h','''#pragma once
#include "freertos/task.h"
typedef int lv_result_t;
typedef int lv_thread_prio_t;
#define LV_RESULT_OK 1
#define LV_RESULT_INVALID 0
extern unsigned log_errors;
#define LV_LOG_ERROR(...) (++log_errors)
typedef struct { void *pTaskArg; TaskFunction_t pvStartRoutine; TaskHandle_t xTaskHandle; } lv_thread_t;
lv_result_t lv_thread_init(lv_thread_t *,const char *,lv_thread_prio_t,TaskFunction_t,size_t,void *);
lv_result_t lv_thread_delete(lv_thread_t *);
''')
    upstream=ROOT/'../dependencies/managed_components/lvgl__lvgl/src/osal/lv_freertos.c'
    source=upstream.read_text(encoding='utf-8')
    # Copy only the unmodified thread functions; mutex/ISR scheduling is outside this test.
    init=re.search(r'^lv_result_t lv_thread_init\([\s\S]*?\n\}',source,re.M).group(0)
    delete=extract(source,'lv_result_t lv_thread_delete(lv_thread_t * pxThread)')
    entry=extract(source,'static void prvRunThread(void * pxArg)')
    write('upstream_threads.inc','#include "lv_test.h"\nstatic void prvRunThread(void *);\n'+init+'\n'+delete+'\n'+entry+'\n')
    write('harness.c',r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lv_test.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
unsigned log_errors;
static unsigned checks,live,fail_next,entries,self_deletes;
static size_t stack_bytes;
static TaskHandle_t current;
struct FakeTask { TaskFunction_t entry;void *arg;size_t bytes; };
#define CHECK(c) do {++checks;if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}} while(0)
BaseType_t xTaskCreate(TaskFunction_t e,const char *n,configSTACK_DEPTH_TYPE b,void *a,UBaseType_t p,TaskHandle_t *t)
{ (void)e;(void)n;(void)b;(void)a;(void)p;(void)t;CHECK(0);return pdFAIL; }
void vTaskDelete(TaskHandle_t t) { (void)t;CHECK(0); }
BaseType_t xTaskCreateWithCaps(TaskFunction_t entry,const char *name,configSTACK_DEPTH_TYPE bytes,
                              void *arg,UBaseType_t priority,TaskHandle_t *task,UBaseType_t caps)
{
    CHECK(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));CHECK(priority==3);CHECK(strcmp(name,"swdraw")==0);
    if(fail_next){fail_next=0;*task=NULL;return pdFAIL;}
    *task=malloc(sizeof(**task));CHECK(*task!=NULL);
    **task=(struct FakeTask){entry,arg,bytes};++live;stack_bytes+=bytes;return pdPASS;
}
void vTaskDeleteWithCaps(TaskHandle_t task)
{
    if(!task){task=current;++self_deletes;}
    CHECK(task!=NULL);CHECK(live>0);CHECK(stack_bytes>=task->bytes);
    --live;stack_bytes-=task->bytes;free(task);
}
static void callback(void *value) {CHECK(value==&entries);++entries;}
int main(void)
{
    for(unsigned i=0;i<300;++i){
        lv_thread_t a={0},b={0};
        CHECK(lv_thread_init(&a,"swdraw",3,callback,40960,&entries)==LV_RESULT_OK);
        CHECK(lv_thread_init(&b,"swdraw",3,callback,40960,&entries)==LV_RESULT_OK);
        CHECK(live==2 && stack_bytes==81920);
        CHECK(a.xTaskHandle->arg==&a && b.xTaskHandle->arg==&b);
        CHECK(lv_thread_delete(&b)==LV_RESULT_OK);
        CHECK(live==1 && stack_bytes==40960);
        if(i%2){current=a.xTaskHandle;current->entry(current->arg);current=NULL;}
        else CHECK(lv_thread_delete(&a)==LV_RESULT_OK);
        CHECK(live==0 && stack_bytes==0);
        fail_next=1;
        CHECK(lv_thread_init(&a,"swdraw",3,callback,40960,&entries)==LV_RESULT_INVALID);
        CHECK(live==0 && stack_bytes==0);
    }
    CHECK(log_errors==300);CHECK(entries==150 && self_deletes==150);
    printf("%u checks; 300 dual-renderer lifecycle/failure cycles; RTOS allocator stubbed\n",checks);
    return 0;
}
''')
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    def execute(name,command,success=True):
        p=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=120,
                         creationflags=subprocess.CREATE_NO_WINDOW)
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if (p.returncode==0)!=success:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-5000:])
        return (p.stdout+p.stderr).decode(errors='replace')
    flags=[str(compiler),'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(run),
           '-DYUMO_LV_FREERTOS_SOURCE="upstream_threads.inc"']
    port=BSP/'lvgl_psram_threads.c'
    exe=run/'lvgl-stacks.exe'
    execute('compile',flags+[str(port),str(run/'harness.c'),'-o',str(exe)])
    result=execute('replay',[str(exe)]).strip()
    for g in GUARDS:
        output=execute('guard-'+g,flags+['-DCONFIG_'+g+'=0','-fsyntax-only',str(port)],False)
        assert 'LVGL PSRAM stacks require XiP' in output
    paths=(port,upstream,Path(__file__).resolve(),BSP/'power_adapter.cmake',BSP/'esp32_s3_touch_lcd_1_54.c')
    receipt=dict(passed=True,output=result,guards=list(GUARDS),
                 inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                 limitations='Actual LVGL thread functions and production adapter; stubbed RTOS, no hardware scheduling, cache/DMA or heap measurement')
    write('result.json',json.dumps(receipt,indent=2));print(result)

if __name__=='__main__':main()
