"""Exercise the production Xiaozhi audio bridge with fake I2S/codec fault injection."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    run, compiler = args.run_dir.resolve(), args.compiler.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve())
    run.mkdir(parents=True, exist_ok=False)
    inputs = ['firmware/main/core/xiaozhi_audio.hpp', 'firmware/main/core/xiaozhi_audio.cpp',
              'firmware/main/core/dual_mic_input.hpp', 'firmware/main/core/recording_level.hpp',
              'tooling/firmware/native/xiaozhi_audio_replay.cpp', 'tooling/firmware/test_xiaozhi_audio_runtime.py']
    hashes = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs}
    (run/'esp_codec_dev.h').write_text('#pragma once\n', encoding='utf-8')
    (run/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n', encoding='utf-8')
    (run/'esp_log.h').write_text('''#pragma once
void xiaozhi_host_log(const char *,const char *,...);
#define ESP_LOGI(...) xiaozhi_host_log(__VA_ARGS__)
''', encoding='utf-8')
    (run/'freertos').mkdir()
    (run/'freertos/FreeRTOS.h').write_text('#pragma once\n', encoding='utf-8')
    (run/'freertos/task.h').write_text(
        '#pragma once\ninline unsigned uxTaskGetStackHighWaterMark(void *){return 32*1024;}\n', encoding='utf-8')
    (run/'esp_heap_caps.h').write_text('''#pragma once
#include <cstddef>
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
void *heap_caps_malloc(size_t,unsigned);
void *heap_caps_calloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
''', encoding='utf-8')
    includes = [run, ROOT/'firmware/main/core']
    codec = ROOT/'dependencies/managed_components/espressif__esp_audio_codec/include'
    includes += [codec, codec/'encoder', codec/'encoder/impl', codec/'decoder', codec/'decoder/impl']
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent)+os.pathsep+env.get('PATH','')
    def execute(name, command):
        p = subprocess.run(command, cwd=run, env=env, capture_output=True, timeout=120,
                           creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:
            raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-7000:])
        return p.stdout.decode().strip()
    exe = run/'xiaozhi-audio.exe'
    execute('compile', [str(compiler), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-static-libgcc', '-static-libstdc++', *['-I'+str(p) for p in includes],
                       str(ROOT/'firmware/main/core/xiaozhi_audio.cpp'),
                       str(ROOT/'tooling/firmware/native/xiaozhi_audio_replay.cpp'), '-o', str(exe)])
    output = execute('replay', [str(exe)])
    result = dict(passed=True, output=output, inputs=hashes, device_accessed=False,
                  production_source_compiled=True, synthetic_codec_and_i2s=True,
                  limits='Tests framing, bounds, conditioning, ownership and cleanup; not Opus quality, real audio or radio timing.')
    (run/'result.json').write_text(json.dumps(result,indent=2)+'\n', encoding='utf-8')
    print(output)

if __name__ == '__main__':
    main()
