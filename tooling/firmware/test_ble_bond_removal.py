"""Replay production bond removal through the pinned SDK's real unpair/delete code."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
NIMBLE = Path(__import__('os').environ['IDF_PATH']) / 'components/bt/host/nimble/nimble/nimble'


def function(source, declaration):
    start = source.index(declaration)
    opening = source.index('{', start)
    # Ignore braces in comments/string literals without changing emitted code.
    tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[opening:])
    depth = 0
    for token in tokens:
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return source[start:opening + token.end()] + '\n'
    raise ValueError('Unterminated function: ' + declaration)


def sha(data):
    return hashlib.sha256(data).hexdigest()


STUBS = r'''#pragma once
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "host/ble_store.h"
#include "replay_errors.h"
#define BLE_HS_LOG(...) ((void)0)
#define BLE_HS_DBG_ASSERT(x) assert(x)
struct ble_hs_conn { int synthetic_id; };
#ifdef __cplusplus
extern "C" {
#endif
int ble_hs_is_enabled(void);
int ble_gap_adv_active(void);
int ble_gap_disc_active(void);
void ble_hs_lock(void);
void ble_hs_unlock(void);
struct ble_hs_conn *ble_hs_conn_find_by_addr(const ble_addr_t *);
int ble_gap_terminate_with_conn(struct ble_hs_conn *, uint8_t);
int ble_hs_pvcy_remove_entry(uint8_t, const uint8_t *);
int ble_gap_conn_cancel(void);
int ble_gap_unpair(const ble_addr_t *);
#ifdef __cplusplus
}
#endif
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', required=True, type=Path)
    args = parser.parse_args()
    run = args.run_dir.resolve()
    if not run.is_relative_to((ROOT / 'artifacts/validation').resolve()):
        raise ValueError('run-dir must stay under artifacts/validation')
    run.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'firmware/main/core/ble_hid.cpp'
    gap = NIMBLE / 'host/src/ble_gap.c'
    util = NIMBLE / 'host/src/ble_store_util.c'
    store_header = NIMBLE / 'host/include/host/ble_store.h'
    ble_header = NIMBLE / 'include/nimble/ble.h'
    hs_header = NIMBLE / 'host/include/host/ble_hs.h'
    harness = ROOT / 'tooling/firmware/native/ble_bond_removal_replay.cpp'
    config_path = ROOT / 'firmware/sdkconfig'
    copied_headers = [ROOT / ('firmware/main/core/' + name) for name in ('ble_hid.hpp', 'ble_peer_store.hpp')]
    inputs = [source, gap, util, store_header, ble_header, hs_header, harness,
              Path(__file__).resolve(), config_path, *copied_headers]
    before = {(str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else '@sdk/'+p.relative_to(Path(__import__('os').environ['IDF_PATH'])).as_posix()): sha(p.read_bytes()) for p in inputs}
    config = config_path.read_text(encoding='utf-8')
    for setting in ('CONFIG_BT_NIMBLE_MAX_BONDS=16', 'CONFIG_BT_NIMBLE_HS_PVCY=y'):
        assert re.search('^' + re.escape(setting) + '$', config, re.M), setting
    for setting in ('CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY', 'CONFIG_BT_NIMBLE_SMP_ID_RESET',
                    'CONFIG_BT_NIMBLE_ENC_ADV_DATA'):
        assert not re.search('^' + setting + '=y$', config, re.M), setting
    (run / 'syscfg').mkdir()
    (run / 'os').mkdir()
    (run / 'core').mkdir()
    for path in copied_headers:
        (run / 'core' / path.name).write_bytes(path.read_bytes())
    (run / 'os/os.h').write_text('#pragma once\n', encoding='utf-8')
    (run / 'syscfg/syscfg.h').write_text('''#pragma once
#define MYNEWT_VAL(x) MYNEWT_VAL_##x
#define NIMBLE_BLE_SM 1
#define NIMBLE_BLE_CONNECT 1
#define MYNEWT_VAL_BLE_STORE_MAX_BONDS 16
#define MYNEWT_VAL_BLE_STORE_MAX_CCCDS 128
#define MYNEWT_VAL_BLE_GATT_CSFC_SIZE 1
#define MYNEWT_VAL_BLE_MULTI_ADV_INSTANCES 0
#define MYNEWT_VAL_BLE_HS_PVCY 1
#define MYNEWT_VAL_BLE_HOST_BASED_PRIVACY 0
#define MYNEWT_VAL_BLE_SMP_ID_RESET 0
#define MYNEWT_VAL_ENC_ADV_DATA 0
''', encoding='utf-8')
    errors = '\n'.join(re.findall(r'^#define BLE_HS_[A-Z0-9_]+\s+\d+\s*$', hs_header.read_text(encoding='utf-8'), re.M))
    (run / 'replay_errors.h').write_text('#pragma once\n' + errors + '\n', encoding='utf-8')
    (run / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_FLASH_OP_FAIL 24578
''', encoding='utf-8')
    (run / 'sdk_bond_stubs.h').write_text(STUBS, encoding='utf-8')
    declarations = ('esp_err_t BleHid::removeBond(', 'esp_err_t BleHid::removeAllBonds(')
    body = source.read_text(encoding='utf-8')
    selected = {decl: function(body, decl) for decl in declarations}
    (run / 'bond_removal.inc').write_text('\n'.join(selected.values()), encoding='utf-8')
    util_body = util.read_text(encoding='utf-8')
    sdk = '#include "sdk_bond_stubs.h"\n'
    # Preserve the SDK's C semantics, including its swallowed persistence error.
    sdk += function(util_body, 'struct ble_store_util_peer_set {').rstrip() + ';\n'
    for decl in ('static int\nble_store_util_iter_unique_peer(', 'int\nble_store_util_bonded_peers(',
                 'int\nble_store_util_delete_all(', 'int\nble_store_util_delete_peer('):
        sdk += function(util_body, decl)
    sdk += function(gap.read_text(encoding='utf-8'), 'int\nble_gap_unpair(')
    (run / 'sdk_bond_removal.c').write_text(sdk, encoding='utf-8')
    ccdir = Path(__import__('shutil').which('g++') or 'g++').resolve().parent
    env = dict(os.environ)
    env['PATH'] = str(ccdir) + os.pathsep + env.get('PATH', '')
    includes = ['-I' + str(run), '-I' + str(NIMBLE / 'host/include'), '-I' + str(NIMBLE / 'include')]

    def execute(name, command):
        result = subprocess.run(list(map(str, command)), cwd=run, env=env, capture_output=True,
                                timeout=120, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run / (name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode(errors='replace')[-7000:])
        return result.stdout.decode().strip()

    execute('compile-sdk', [ccdir / 'gcc.exe', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                           *includes, '-c', run / 'sdk_bond_removal.c', '-o', run / 'sdk.o'])
    execute('compile', [ccdir / 'g++.exe', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                       *includes, harness, run / 'sdk.o', '-static-libgcc', '-static-libstdc++',
                       '-o', run / 'bond-removal.exe'])
    output = execute('replay', [run / 'bond-removal.exe'])
    current = source.read_text(encoding='utf-8')
    if any(function(current, decl) != text for decl, text in selected.items()):
        raise RuntimeError('Production removal functions changed during replay; rerun')
    after = {(str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else '@sdk/'+p.relative_to(Path(__import__('os').environ['IDF_PATH'])).as_posix()): sha(p.read_bytes()) for p in inputs}
    if any(before[key] != value for key, value in after.items() if key != (str(source.relative_to(ROOT)) if source.is_relative_to(ROOT) else '@sdk/'+source.relative_to(Path(__import__('os').environ['IDF_PATH'])).as_posix())):
        raise RuntimeError('An input other than unrelated ble_hid code changed during replay; rerun')
    result = json.loads(output)
    result.update(inputs=before, inputs_after=after,
                  production_functions={decl: sha(text.encode()) for decl, text in selected.items()},
                  sdk_unpair_and_delete_and_enumeration_extracted=True, device_accessed=False,
                  limits='Real app removal and SDK C unpair/delete/enumeration; synthetic store failures and metadata. No flash durability or RF claim.')
    (run / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
