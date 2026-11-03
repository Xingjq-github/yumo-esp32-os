"""Replay production button callbacks and sleep commit with deterministic thread interleavings.

Only GPIO/RTOS/services are stubbed. No firmware build, flash, or device access.
"""
from pathlib import Path
import argparse, ast, hashlib, json, os, subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True, help='External host g++ compiler path')
    parser.add_argument('--before', type=Path, help='Optional pre-change source root for regression proof')
    args = parser.parse_args()
    run = args.run_dir.resolve()
    assert run.is_relative_to((ROOT / 'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    cc = args.compiler.resolve()
    env = dict(os.environ)
    env['PATH'] = str(cc.parent) + os.pathsep + env.get('PATH', '')
    # Reuse the established board stubs, without building LVGL/the stopwatch UI.
    tree = ast.parse((ROOT / 'tooling/firmware/test_timer_ui.py').read_text(encoding='utf8'))
    files = next(ast.literal_eval(node.value) for node in ast.walk(tree)
                 if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'files' for t in node.targets))
    for name, source in files.items():
        if name == 'lv_conf.h':
            continue
        dest = run / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source, encoding='utf8')
    (run / 'esp_err.h').write_text('#pragma once\nusing esp_err_t=int;\nconstexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2;\n')

    outputs = {}
    hashes = {}
    for label, root in [('after', ROOT)] + ([('before', args.before.resolve())] if args.before else []):
        source = (root / 'firmware/main/core/button_mgr.cpp').read_text(encoding='utf8')
        for header in ('settings_store.hpp', 'system_service.hpp', 'screenshot.hpp'):
            source = source.replace('#include "' + header + '"', '#include "host_services.hpp"')
        (run / 'production_buttons.cpp').write_text(source, encoding='utf8')
        power = (root / 'firmware/main/core/power_mgr.cpp').read_text(encoding='utf8')
        def part(start, end):
            begin = power.index(start)
            return power[begin:power.index(end, begin)]
        methods = part('esp_err_t Power::shutdown()', 'void Power::standby(')
        methods += part('esp_err_t Power::deepSleep(', 'esp_err_t Power::restartForTheme(')
        methods += part('void Power::standby(', 'bool Power::sleepPending(')
        methods += part('void Power::processStandby()', 'void Power::syncTouch(')
        methods += part('void Power::toggleScreen()', 'bool Power::isScreenOn(')
        runtime = (ROOT / 'firmware/main/core/power_runtime.cpp').read_text(encoding='utf8')
        methods += runtime[runtime.index('bool Power::automaticDeepSleepAllowed()'):runtime.index('bool Power::automaticScreenOffAllowed()')]
        (run / 'shutdown.inc').write_text(methods, encoding='utf8')
        baseline = (ROOT / 'tooling/firmware/native/system_shutdown_replay.cpp').read_text(encoding='utf8')
        baseline = baseline.replace('wakeRequested{false},', '')
        baseline = baseline.replace('namespace sys {', '''namespace sys {
static void (*wakeLoadHook)()=nullptr;
static void (*suspendHook)()=nullptr;
struct HookBool:std::atomic<bool> {
    using std::atomic<bool>::atomic;
    using std::atomic<bool>::operator=;
    bool load() const { const bool value=std::atomic<bool>::load();if(wakeLoadHook)wakeLoadHook();return value; }
};
static HookBool wakeRequested{false};
static std::atomic<bool> automaticDark{false};
static std::atomic<bool> screen{true};
void Power::activity() {}
''', 1)
        baseline = baseline.replace('protect(lateProtection);', 'protect(lateProtection);if(suspendHook)suspendHook();')
        baseline = baseline.replace('lit=on;', 'lit=on;screen=on;')
        baseline = baseline.replace('int main(){', 'int shutdownBaselineMain(){')
        baseline = baseline.replace('checks);\n}', 'checks);return 0;\n}')
        (run / 'shutdown_baseline.inc').write_text(baseline, encoding='utf8')
        for case in ('buttons', 'sleep'):
            exe = run / f'{label}-{case}.exe'
            native = ROOT / 'tooling/firmware/native' / f'{case}_ownership_replay.cpp'
            cmd = [str(cc), '-std=c++17', '-O1', '-Wall', '-Wextra', '-pthread', '-static-libgcc', '-static-libstdc++',
                   '-I' + str(run), '-I' + str(ROOT / 'firmware/main'), '-I' + str(ROOT / 'firmware/main/core'),
                   str(native), '-o', str(exe)]
            if label == 'before':
                cmd.append('-DEXPECT_OLD=1')
            if case == 'buttons':
                cmd.append(str(run / 'production_buttons.cpp'))
            for stage, command in [('compile', cmd), ('replay', [str(exe)])]:
                result = subprocess.run(command, cwd=run, env=env, capture_output=True, timeout=90,
                                        creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                (run / f'{label}-{case}-{stage}.log').write_bytes(result.stdout + result.stderr)
                if result.returncode:
                    raise RuntimeError((result.stdout + result.stderr).decode(errors='replace'))
            outputs[f'{label}-{case}'] = result.stdout.decode().strip()
        hashes[label] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in (
            'firmware/main/core/button_mgr.cpp', 'firmware/main/core/power_mgr.cpp')}
    report = dict(passed=True, outputs=outputs, inputs=hashes, device_accessed=False,
                  method='Production callbacks and full processStandby with board stubs; real mutex/threads and deterministic final-check hook')
    (run / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
