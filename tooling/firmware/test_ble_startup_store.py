"""Replay production BLE startup storage gate without creating a BLE host."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.run_dir.resolve()
    if not output.is_relative_to((root / "artifacts/validation").resolve()):
        raise ValueError("run-dir must be under artifacts/validation")
    output.mkdir(parents=True, exist_ok=False)
    source = root / "firmware/main/core/ble_hid.cpp"
    text = source.read_text(encoding="utf-8")
    begin, end = text.index("esp_err_t BleHid::init(void)"), text.index("esp_err_t BleHid::setEnabled(bool enabled)")
    body = text[begin:end]
    (output / "ble_startup.inc").write_text(body, encoding="utf-8")
    (output / "esp_err.h").write_text("#pragma once\nusing esp_err_t = int;\n", encoding="utf-8")
    compiler = Path(__import__('shutil').which('g++') or 'g++').resolve()
    native = root / "tooling/firmware/native/ble_startup_store_replay.cpp"
    exe = output / "ble_startup_store.exe"
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    command = [str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(output), "-I" + str(root / "firmware/main"),
               str(native), "-o", str(exe)]
    compile_result = subprocess.run(command, env=env, capture_output=True)
    (output / "compile.log").write_bytes(compile_result.stdout + compile_result.stderr)
    if compile_result.returncode:
        raise RuntimeError((compile_result.stdout + compile_result.stderr).decode(errors="replace"))
    result = subprocess.run([str(exe)], env=env, capture_output=True)
    (output / "run.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
    receipt = json.loads(result.stdout)
    receipt.update(production_source_extracted=True, command=command,
        init_body_sha256=hashlib.sha256(body.encode()).hexdigest(),
        inputs={str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in [source, native, Path(__file__).resolve()]},
        limits="Platform and store-status stubs; verifies real init ordering, publication and cleanup, not radio or storage implementation.")
    (output / "result.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(result.stdout.decode())


if __name__ == "__main__":
    main()
