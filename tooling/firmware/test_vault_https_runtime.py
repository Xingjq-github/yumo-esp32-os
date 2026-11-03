"""Run production vault HTTP(S) request code against a deterministic host transport.

This tests request configuration, authentication, errors and bounded transfer
cleanup. It never opens a network connection, serial port, real deployment key
or vault. The native fixture contains an explicitly synthetic deployment key;
Python supplies an independent SHA-256 oracle for its expected proof.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--run-dir", required=True, type=Path)
    args = parser.parse_args()
    compiler, run = args.compiler.resolve(), args.run_dir.resolve()
    if not run.is_relative_to((ROOT / "artifacts/validation").resolve()):
        raise ValueError("run-dir must be inside this project's artifacts/validation")
    run.mkdir(parents=True, exist_ok=False)
    core = ROOT / "firmware/main/core"
    sync_file, json_file = core / "vault_sync.cpp", core / "vault_json.cpp"
    sync_source = sync_file.read_text(encoding="utf8")
    # Only preprocessor include directives are removed: the entire real request
    # implementation and its internal helpers are then compiled byte for byte.
    (run / "vault_sync.inc").write_text(
        re.sub(r"^#include [^\n]*\n", "", sync_source, flags=re.M), encoding="utf8"
    )
    json_source = json_file.read_text(encoding="utf8")
    origin_header = core / "vault_origin.hpp"
    if origin_header.exists():
        origin_code = '#include "core/vault_origin.hpp"\n'
        origin_source_files = [origin_header]
    else:
        origin_code = "namespace vault {\n" + "\n".join(
            function(json_source, signature)
            for signature in ("bool normalizeOrigin(", "bool validOrigin(")
        ) + "\n}\n"
        origin_source_files = [json_file]
    (run / "vault_origin.inc").write_text(origin_code, encoding="utf8")
    # This public fixture key is deliberately unrelated to the device identity.
    proof = hashlib.sha256(b"JYVAULT-DEPLOY-KEY-V2" + bytes(range(32))).hexdigest()
    (run / "oracle.hpp").write_text(
        '#pragma once\n#define FIXTURE_EXPECTED_PROOF "' + proof + '"\n', encoding="ascii"
    )
    fixture = ROOT / "tooling/firmware/native/vault_https_replay.cpp"
    sha_file = core / "vault_sha256.c"
    executable = run / ("vault-https.exe" if os.name == "nt" else "vault-https")
    command = [str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra",
               "-static-libgcc", "-static-libstdc++", "-I" + str(run),
               "-I" + str(core.parent), str(fixture), str(sha_file),
               "-o", str(executable)]
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    options = dict(cwd=run, env=env, capture_output=True, timeout=100)
    if os.name == "nt":
        options["creationflags"] = subprocess.CREATE_NO_WINDOW
    for stage, cmd in (("compile", command), ("execute", [str(executable)])):
        result = subprocess.run(cmd, **options)
        (run / (stage + ".stdout.log")).write_bytes(result.stdout)
        (run / (stage + ".stderr.log")).write_bytes(result.stderr)
        if result.returncode:
            print(result.stdout.decode("utf8", errors="replace")[-10000:])
            print(result.stderr.decode("utf8", errors="replace")[-10000:])
            raise RuntimeError(stage + " failed; logs: " + str(run))
    cases = [json.loads(line) for line in result.stdout.decode("utf8").splitlines() if line]
    sources = [sync_file, *origin_source_files, fixture, sha_file,
               core / "vault_crypto.hpp", core / "vault_format.hpp", Path(__file__).resolve()]
    evidence = dict(
        passed=all(case["passed"] for case in cases), scenarios=len(cases),
        checks=sum(case["checks"] for case in cases), cases=cases,
        source_sha256={str(p.relative_to(ROOT)).replace("\\", "/"):
                       hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        method="Entire production vault_sync.cpp without includes; production origin parser and SHA-256; fake ESP HTTP client, Wi-Fi, power and clocks; synthetic deployment key/token.",
        limits="Verifies application behavior and TLS configuration, not real DNS, certificate chain validation, ESP-IDF transport, memory pressure, firmware timing or hardware synchronization.",
        network_accessed=False, device_accessed=False, production_keys_accessed=False,
    )
    (run / "result.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf8")
    print(json.dumps({k: evidence[k] for k in ("passed", "scenarios", "checks")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
