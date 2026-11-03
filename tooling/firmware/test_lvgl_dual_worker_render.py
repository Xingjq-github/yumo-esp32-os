"""Fresh Windows two-worker LVGL build; real production background/pattern replays.

Host rendering only. Never opens device ports or invokes firmware builds.
"""
from pathlib import Path
import argparse, ast, hashlib, json, os, re, subprocess, sys, time
import test_vault_pattern_render as pattern

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--run-dir', type=Path, required=True)
    p.add_argument('--lvgl-root', type=Path, required=True, help='External pinned LVGL source directory')
    p.add_argument('--resume', action='store_true', help='Continue only this same Windows/two-worker run after timeout')
    a = p.parse_args()
    run = a.run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation/vault-display-response-20261030').resolve())
    run.mkdir(parents=True, exist_ok=a.resume)
    cache = run/'c-objects'; cache.mkdir(exist_ok=a.resume)
    lv = a.lvgl_root.resolve()
    config = (lv/'lv_conf_template.h').read_text(encoding='utf8')
    config = config.replace('#if 0 /* Set this to "1" to enable content */', '#if 1')
    config = re.sub(r'#define LV_MEM_SIZE [^\n]+', '#define LV_MEM_SIZE (4 * 1024 * 1024U)', config)
    config = re.sub(r'#define LV_USE_OS [^\n]+', '#define LV_USE_OS LV_OS_WINDOWS', config)
    config = re.sub(r'#define LV_DRAW_SW_DRAW_UNIT_CNT [^\n]+', '#define LV_DRAW_SW_DRAW_UNIT_CNT 2', config)
    if a.resume:
        assert (cache/'lv_conf.h').read_text(encoding='utf8') == config
        previous=json.loads((run/'result.json').read_text())
        assert previous['os']=='LV_OS_WINDOWS' and previous['draw_workers']==2 and not previous['c_objects_reused']
    else:
        (cache/'lv_conf.h').write_text(config, encoding='utf8')
    files = [f for f in (lv/'src').rglob('*.c') if not set(f.relative_to(lv/'src').parts)&{'libs','drivers'}]
    files += list((lv/'src/libs/bin_decoder').glob('*.c'))
    assert len({f.stem for f in files}) == len(files)
    compiler = a.compiler.resolve()
    env = dict(os.environ); env['PATH'] = str(compiler.parent)+os.pathsep+env.get('PATH','')
    started = time.time(); results = {}; hashes = {}
    status = dict(passed=False,stage='created',os='LV_OS_WINDOWS',draw_workers=2,c_objects_reused=False,
                  firmware_built=False,device_accessed=False,buffer_rows=20,double_buffer=True,
                  host_exit='flush report, lv_deinit, then _Exit; bypass Windows CRT/global teardown after LVGL TerminateThread')

    def write_status():
        (run/'result.json').write_text(json.dumps(dict(status,results=results,inputs=hashes,elapsed_seconds=time.time()-started),indent=2))

    def execute(name, command, cwd, timeout=600):
        status['stage'] = name; write_status()
        try:
            outcome = subprocess.run(command,cwd=cwd,env=env,capture_output=True,timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (run/(name+'.stdout.log')).write_bytes(error.stdout or b'')
            (run/(name+'.stderr.log')).write_bytes(error.stderr or b'')
            status['timeout_seconds']=timeout;write_status();raise
        (run/(name+'.stdout.log')).write_bytes(outcome.stdout)
        (run/(name+'.stderr.log')).write_bytes(outcome.stderr)
        results[name] = dict(exit_code=outcome.returncode)
        write_status()
        if outcome.returncode:
            print(outcome.stderr.decode(errors='replace')[-16000:]); raise RuntimeError(name+' failed')
        return outcome.stdout.decode(errors='replace')

    def rsp(path, values):
        path.write_text('\n'.join('"'+v.replace('\\','/')+'"' for v in values),encoding='utf8')
        return '@'+str(path)

    common=['-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(cache),'-I'+str(lv)]
    pending=[f for f in files if not a.resume or not (cache/(f.stem+'.o')).is_file() or (cache/(f.stem+'.o')).stat().st_mtime_ns < f.stat().st_mtime_ns]
    status['same_run_objects_already_completed']=len(files)-len(pending)
    if pending:
        args=rsp(cache/'compile-remaining.rsp',common+['-c']+list(map(str,pending)))
        execute('compile-fresh-c-remaining' if a.resume else 'compile-fresh-c',[str(compiler.with_name('gcc.exe')),args],cache)
    status['fresh_c_objects'] = len(files); write_status()
    objects=[str(cache/(f.stem+'.o')) for f in files]
    assert all(Path(o).is_file() for o in objects)

    background=run/'background'; background.mkdir(exist_ok=a.resume)
    for name in ('window_mgr','vault_pattern'):
        src=ROOT/'firmware/main/ui'/(name+'.cpp')
        hashes[str(src.relative_to(ROOT))]=hashlib.sha256(src.read_bytes()).hexdigest()
        (background/(name+'.inc')).write_text(re.sub(r'^#include .*\n','',src.read_text(encoding='utf8'),flags=re.M),encoding='utf8')
    src=ROOT/'tooling/firmware/native/app_background_replay.cpp'
    body=src.read_text(encoding='utf8')
    # Exercise the actual status-bar radio labels on the home screen.
    body=body.replace('static Wifi snapshot(){return {};}', 'static Wifi snapshot(){return {true,false,WifiState::Connected};}')
    body=body.replace('static Bluetooth bluetooth(){return {};}', 'static Bluetooth bluetooth(){return {true,BleHidState::Connected};}')
    body=body.replace('int main(){','int main(){\n    static_assert(LV_USE_OS==LV_OS_WINDOWS && LV_DRAW_SW_DRAW_UNIT_CNT==2);')
    body=body.replace('auto homeScreen=lv_screen_active();','auto homeScreen=lv_screen_active();CHECK(std::strcmp(lv_label_get_text(ui::s_statusWifi),LV_SYMBOL_WIFI)==0);CHECK(std::strcmp(lv_label_get_text(ui::s_statusBluetooth),LV_SYMBOL_BLUETOOTH)==0);')
    body=body.replace('lv_area_t tiny={42,42,66,66};','lv_area_t tiny={96,0,149,23};')
    body=body.replace('    // Allocation,', '    fprintf(stderr,"phase: 80 navigation cycles and pixel comparisons passed\\n");fflush(stderr);\n    // Allocation,')
    body=body.replace('    // A top-layer', '    fprintf(stderr,"phase: 40 lifecycle failure cycles passed\\n");fflush(stderr);\n    // A top-layer')
    body=body.replace('wm.goHome();lv_display_delete(d);lv_deinit();printf', 'fprintf(stderr,"phase: notice lifetime passed\\n");fflush(stderr);wm.goHome();fprintf(stderr,"phase: delete display\\n");fflush(stderr);lv_display_delete(d);printf')
    host_exit='\nfflush(stdout);fprintf(stderr,"phase: deinit LVGL\\n");fflush(stderr);lv_deinit();fprintf(stderr,"phase: deinit complete; explicit host exit\\n");fflush(stderr);std::_Exit(0);\n'
    body=body.rsplit('}',1)[0]+host_exit+'}\n'
    (background/'replay.cpp').write_text(body,encoding='utf8')
    exe=background/'replay.exe'
    args=rsp(background/'link.rsp',common+['-std=c++17','-I'+str(background),'-I'+str(ROOT/'firmware/main'),'-o',str(exe),str(background/'replay.cpp')]+objects)
    execute('compile-background',[str(compiler),args],background)
    results['background']=json.loads(execute('run-background',[str(exe)],background,20).strip().splitlines()[-1])
    write_status()

    tracing=run/'pattern'; tracing.mkdir(exist_ok=a.resume); (tracing/'production').mkdir(exist_ok=a.resume); (tracing/'core').mkdir(exist_ok=a.resume)
    for name in ('ui/vault_pattern.cpp','ui/vault_pattern.hpp','core/vault_pattern.hpp','core/vault_format.hpp'):
        src=ROOT/'firmware/main'/name
        hashes[str(src.relative_to(ROOT))]=hashlib.sha256(src.read_bytes()).hexdigest()
        target=tracing/'production'/Path(name).name if name.startswith('ui/') else tracing/name
        target.write_bytes(src.read_bytes())
    syntax=ast.parse(Path(pattern.__file__).read_text(encoding='utf8'))
    stubs=next(ast.literal_eval(n.value) for n in ast.walk(syntax) if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='stubs' for x in n.targets))
    for name,text in stubs.items(): (tracing/name).write_text(text,encoding='utf8')
    harness=pattern.HARNESS.replace('w==45','w==60').replace('15px / 45px','15px / 60px').replace('halo_diameter\\\":45','halo_diameter\\\":60')
    harness=harness.replace('int main() {','int main() {\n    static_assert(LV_USE_OS==LV_OS_WINDOWS && LV_DRAW_SW_DRAW_UNIT_CNT==2);')
    harness=harness.replace('lv_deinit();','')
    harness=harness.rsplit('}',1)[0]+host_exit+'}\n'
    (tracing/'replay.cpp').write_text(harness,encoding='utf8')
    exe=tracing/'replay.exe'
    args=rsp(tracing/'link.rsp',common+['-std=c++17','-I'+str(tracing),'-o',str(exe),str(tracing/'replay.cpp')]+objects)
    execute('compile-pattern',[str(compiler),args],tracing)
    results['pattern']=json.loads(execute('run-pattern',[str(exe)],tracing,120).strip().splitlines()[-1])
    status['stage']='complete';status['passed']=True;write_status()
    print(json.dumps(dict(status,results=results)))


if __name__=='__main__': main()
