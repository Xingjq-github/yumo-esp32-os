#!/usr/bin/env python3
"""Build real ESP-IDF NVS C API against a deterministic 16KiB host NOR flash.

Other replay drivers may import build_host(root, output, sources, ...).
SDK source files are never copied, modified, or built in firmware/build.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

SDK_SOURCES = (
    "nvs_item_hash_list.cpp", "nvs_page.cpp", "nvs_pagemanager.cpp",
    "nvs_storage.cpp", "nvs_types.cpp", "nvs_api.cpp",
    "nvs_handle_simple.cpp", "nvs_partition_manager.cpp",
    "nvs_partition.cpp", "nvs_partition_lookup.cpp", "nvs_platform.cpp",
)

SHIMS = {
    "spi_flash_mmap.h": "#pragma once\n#define ESP_ERR_FLASH_OP_FAIL 0x6001\n",
    "sdkconfig.h": "#pragma once\n#define CONFIG_NVS_ASSERT_ERROR_CHECK 1\n",
    "esp_attr.h": "#pragma once\n#define IDF_DEPRECATED(x)\n#define IRAM_ATTR\n",
    "esp_heap_caps.h": "#pragma once\n#include <stdlib.h>\n",
    "esp_log.h": "#pragma once\n" + "\n".join(
        f"#define ESP_LOG{x}(...)" for x in "EWIDV") + "\n",
    "esp_rom_crc.h": """#pragma once
#include <stdint.h>
inline uint32_t esp_rom_crc32_le(uint32_t c,const uint8_t* p,uint32_t n) {
    c=~c;
    while(n--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1)^((c&1)?0xedb88320u:0); }
    return ~c;
}
""",
    "compat.h": """#pragma once
#include <string.h>
#include <stdint.h>
inline size_t yumo_strlcpy(char*d,const char*s,size_t n) {
    size_t k=strlen(s);
    if(n) { size_t m=k<n-1?k:n-1;memcpy(d,s,m);d[m]=0; }
    return k;
}
#define strlcpy yumo_strlcpy
""",
    "esp_partition.h": """#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef int esp_partition_type_t;
typedef int esp_partition_subtype_t;
typedef struct esp_blockdev* esp_blockdev_handle_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
typedef struct {
    uint32_t address;
    uint32_t size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;
#ifdef __cplusplus
extern "C" {
#endif
const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char*);
esp_err_t esp_partition_read_raw(const esp_partition_t*,size_t,void*,size_t);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
esp_err_t esp_partition_write_raw(const esp_partition_t*,size_t,const void*,size_t);
esp_err_t esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
#ifdef __cplusplus
}
#endif
""",
}


def build_host(root: Path, output: Path, sources, *, name="nvs_capacity",
               include_dirs=(), defines=(), extra_flags=(), sdk_source_overrides=None):
    """Compile SDK NVS+host flash+caller source(s); return exe/env/receipt paths.

    include_dirs are searched before generated OS headers so a caller can supply
    additional NimBLE stubs/sdkconfig, but should not replace NVS API headers.
    No NVS C API is mocked: open/read/write/erase/commit/stats/deinit are official.
    """
    root, output = Path(root).resolve(), Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    inc = output / "host_include"
    inc.mkdir(exist_ok=True)
    for filename, body in SHIMS.items():
        (inc / filename).write_text(body, encoding="utf-8")
    sdk = Path(__import__('os').environ['IDF_PATH']) / 'components'
    nvs = sdk / "nvs_flash"
    native = root / "tooling/firmware/native"
    compiler = Path(__import__('shutil').which('g++') or 'g++').resolve()
    exe = output / (name + ".exe")
    command = [str(compiler), "-std=c++17", "-O1", "-g", "-DNO_DEBUG_STORAGE",
               "-DLINUX_TARGET=1", "-DLINUX_HOST_LEGACY_TEST=1",
               "-include", str(inc / "compat.h")]
    for directory in [*map(Path, include_dirs), inc, native, nvs / "src",
                      nvs / "include", nvs / "private_include", sdk / "esp_common/include"]:
        command += ["-I", str(directory)]
    command += ["-D" + definition for definition in defines]
    command += list(extra_flags)
    originals = [nvs / "src" / filename for filename in SDK_SOURCES]
    overrides = sdk_source_overrides or {}
    paths = [Path(overrides.get(path.name, path)) for path in originals]
    paths += [native / "nvs_capacity_host.cpp", *map(Path, sources)]
    command += [str(path) for path in paths]
    command += ["-o", str(exe)]
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    def input_name(path):
        if path.is_relative_to(root):
            return path.relative_to(root).as_posix()
        return '@sdk/'+path.relative_to(sdk.parent).as_posix()
    hashes = {input_name(path): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in paths}
    receipt = {"partition_bytes": 16384, "inputs_sha256": hashes,
               "sdk_original_sources_sha256": {
                   input_name(path): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in originals},
               "sdk_source_overrides": {name: str(path) for name, path in overrides.items()},
               "command": command, "platform": "Windows deterministic single-thread host",
               "substitutions": ["partition NOR RAM flash", "OS log/heap/header stubs",
                   "CRC32 LE software equivalent", "thin flash init/erase routing",
                   "ESP-IDF Linux-target no-op Lock"],
               "fault_model": "immediate C++ exception at mutation boundary or byte prefix; sequential-prefix erase"}
    process = subprocess.run(command, env=env, capture_output=True)
    (output / "compile.log").write_bytes(process.stdout + process.stderr)
    receipt["compile_exit"] = process.returncode
    receipt_path = output / "build-receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    if process.returncode:
        raise RuntimeError((process.stdout + process.stderr).decode(errors="replace"))
    for path in paths:
        assert hashes[input_name(path)] == hashlib.sha256(path.read_bytes()).hexdigest()
    return exe, env, receipt_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--page-source", type=Path,
        help="explicit project build-copy nvs_page.cpp for differential regression")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    exe, env, receipt = build_host(root, args.output,
        [root / "tooling/firmware/native/nvs_capacity_smoke.cpp"], name="nvs_capacity_smoke",
        sdk_source_overrides={"nvs_page.cpp": args.page_source.resolve()} if args.page_source else None)
    process = subprocess.run([str(exe), str(args.output.resolve())], env=env, capture_output=True)
    (args.output / "run.log").write_bytes(process.stdout + process.stderr)
    (args.output / "run-receipt.json").write_text(json.dumps({
        "exit_code": process.returncode, "passed": process.returncode == 0,
        "build_receipt": str(receipt), "sdk_page_override": str(args.page_source)}, indent=2), encoding="utf-8")
    print((process.stdout + process.stderr).decode(errors="replace"))
    if process.returncode:
        raise SystemExit(process.returncode)
    print(f"Build receipt: {receipt}")


if __name__ == "__main__":
    main()
