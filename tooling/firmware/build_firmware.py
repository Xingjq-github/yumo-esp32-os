"""Build with an activated external ESP-IDF environment; never flash automatically."""
from pathlib import Path
from datetime import datetime
import argparse, hashlib, json, os, subprocess, sys

WORKSPACE = Path(__file__).resolve().parents[2]
ROOT = WORKSPACE/'firmware'
BUILD = WORKSPACE/'artifacts/firmware'
sys.path.insert(0,str(WORKSPACE/'tooling'))
from configure_environment import prepare_build, main as check_environment
from environment import sdk_path, executable
SDK = sdk_path()
PY = Path(sys.executable).resolve()
CMAKE = executable('cmake')
NINJA = executable('ninja')
XTENSA = executable('xtensa-esp-elf-gcc').parent
TOOLS = Path(os.environ.get('IDF_TOOLS_PATH', Path.home()/'.espressif'))

def fingerprint(path):
    raw=path.read_bytes()
    return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir',type=Path)
    parser.add_argument('--stage',choices=('all','configure','checks','models','build'),default='all')
    args=parser.parse_args()
    run=(args.run_dir or WORKSPACE/'artifacts/validation'/datetime.now().strftime('build-%Y%m%d-%H%M%S')).resolve()
    assert run.is_relative_to((WORKSPACE/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=True)
    def save(name,value): (run/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
    env=os.environ.copy()
    env.update(IDF_PATH=str(SDK),IDF_TOOLS_PATH=str(TOOLS),
               IDF_COMPONENT_MANAGER='1',ESP_IDF_VERSION='6.1',PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',PYTHONNOUSERSITE='1',
               CCACHE_DISABLE='1',IDF_COMPONENT_MANAGER_CACHE_PATH=str(BUILD/'Espressif/ComponentManager/Cache'))
    env['PATH']=os.pathsep.join(map(str,(PY.parent,CMAKE.parent,NINJA.parent,XTENSA)))+os.pathsep+env.get('PATH','')
    for path in (PY,CMAKE,NINJA,SDK/'CMakeLists.txt'): assert path.exists(),path
    def execute(name,cmd,cwd=ROOT,expected=0):
        if (run/(name+'.status.json')).exists():
            raise RuntimeError(f'{name} already has a result; use a new run directory')
        state={'status':'running','started':datetime.now().astimezone().isoformat(),'command':cmd,'cwd':str(cwd)}
        save(name+'.status.json',state)
        with (run/(name+'.stdout.log')).open('wb') as out,(run/(name+'.stderr.log')).open('wb') as err:
            process=subprocess.Popen(cmd,cwd=cwd,env=env,stdout=out,stderr=err,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            state['pid']=process.pid;save(name+'.status.json',state);code=process.wait()
        state.update(status='succeeded' if code==expected else 'failed',exit_code=code,finished=datetime.now().astimezone().isoformat())
        save(name+'.status.json',state)
        if code!=expected: raise RuntimeError(f'{name} failed ({code}); see {run}')
        print(name+' succeeded',flush=True)
    def path_checks():
        project=json.loads((BUILD/'project_description.json').read_text(encoding='utf-8'))
        assert Path(project['project_path']).resolve()==ROOT.resolve()
        for item in json.loads((BUILD/'compile_commands.json').read_text(encoding='utf-8')):
            p=Path(item['file']).resolve()
            assert p.is_relative_to(WORKSPACE.resolve()) or p.is_relative_to(SDK), p
        save('path-checks.json',{'passed':True,'project':str(ROOT),'build':str(BUILD),'sdk':str(SDK)})
    def configure():
        prepare_build()
        check_environment()
        if not (ROOT/'main/core/vault_builtin.hpp').is_file():
            raise RuntimeError('Run python tooling/init_config.py before building.')
        execute('configure',[str(CMAKE),'-S',str(ROOT),'-B',str(BUILD),'-G','Ninja',
            '-DESP_PLATFORM=1','-DIDF_TARGET=esp32s3','-DCCACHE_ENABLE=0',f'-DPYTHON={PY}',
            f'-DSDKCONFIG={ROOT/"sdkconfig"}',f'-DCMAKE_MAKE_PROGRAM={NINJA}'])
        path_checks()
    def resources():
        execute('chiptune-resources',[str(PY),'-B',str(WORKSPACE/'tooling/firmware/build_chiptune_samples.py')])
        execute('knowledge-resources',[str(PY),'-B',str(WORKSPACE/'tooling/firmware/build_knowledge.py')])
        execute('pattern-resources',[str(PY),'-B',str(WORKSPACE/'tooling/firmware/build_pattern_game.py'),'--run-dir',str(run/'pattern-resources')])
    def checks():
        execute('source-checks',[str(PY),'-X','utf8','-B',str(WORKSPACE/'tooling/firmware/run_source_checks.py'),
            '--report',str(run/'source-checks.json')])
    def models():
        commands=json.loads((BUILD/'compile_commands.json').read_text(encoding='utf-8'))
        def flags(suffix):
            item=next(x for x in commands if x['file'].replace('\\','/').endswith(suffix))
            return item['command'].rsplit(' -o ',1)[0]+' -fsyntax-only -fdiagnostics-color=never '
        cpp=flags('/main/main.cpp');c=flags('/main/core/gbk_table.c');results=[]
        for path in sorted((WORKSPACE/'tooling/firmware').glob('*_test.*')):
            if path.suffix not in ('.c','.cpp'):continue
            execute('model-'+path.stem,(cpp if path.suffix=='.cpp' else c)+subprocess.list2cmdline([str(path)]),BUILD)
            results.append(path.relative_to(WORKSPACE).as_posix())
        guards=[]
        for macro in ('CONFIG_SPIRAM_FETCH_INSTRUCTIONS','CONFIG_SPIRAM_RODATA','CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY'):
            path=run/('guard_'+macro+'.cpp')
            path.write_text('#include "sdkconfig.h"\n#undef '+macro+'\n#define '+macro+' 0\n#include "core/password_vault.cpp"\n',encoding='utf-8')
            name='guard-'+macro
            execute(name,cpp+subprocess.list2cmdline([str(path)]),BUILD,expected=1)
            assert 'Vault PSRAM stack requires XiP' in (run/(name+'.stderr.log')).read_text(encoding='utf-8',errors='replace')
            guards.append(macro)
        save('models.json',{'passed':True,'method':'ESP32 compiler, compile-time assertions','models':results,'guards':guards})
    def build():
        resources()
        path_checks()
        paths=[p for d in ('main','components') for p in (ROOT/d).rglob('*') if p.is_file()]
        paths += [ROOT/name for name in ('CMakeLists.txt','dependencies.cmake','sdkconfig','sdkconfig.defaults','partitions.csv','dependencies.lock','vault-sync-key.properties')]
        paths += [p for p in (ROOT/'cmake').rglob('*') if p.is_file()]
        before={p.relative_to(ROOT).as_posix():fingerprint(p) for p in paths}
        save('build-inputs.json',{'files':before})
        execute('build',[str(NINJA),'-C',str(BUILD),'-j','2'])
        for name,value in before.items(): assert fingerprint(ROOT/name)==value,name
        path_checks()
        app=BUILD/'01_factory.bin'
        from prepare_vault_flash import partition_layout
        layout=partition_layout((BUILD/'partition_table/partition-table.bin').read_bytes())
        factory=next(row for row in layout if row[0]=='factory')
        assert app.stat().st_size<=factory[4]
        save('build-result.json',{'passed':True,'time':datetime.now().astimezone().isoformat(),
            'app':fingerprint(app),'factory_free':factory[4]-app.stat().st_size,
            'ota_available':False,'flashed':False})
    actions={'configure':configure,'checks':checks,'models':models,'build':build}
    # Initial source checks inspect the generated partition binary as well.
    stages=('configure','build','checks','models') if args.stage=='all' else (args.stage,)
    try:
        for stage in stages: actions[stage]()
        save(args.stage+'.complete.json',{'passed':True,'time':datetime.now().astimezone().isoformat()})
    except Exception as error:
        save(args.stage+'.complete.json',{'passed':False,'error':str(error)})
        raise

if __name__=='__main__': main()
