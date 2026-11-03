"""Build an offline Windows x64 app-only update bundle from a verified build.

Use an activated Windows x64 Python 3.11 (external installation or local venv)
with esptool 5.4 installed. Read this checkout's artifacts/firmware and its own
complete build receipt. This only packages files; no serial port is opened, no
device is modified, and no dependencies are downloaded.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import importlib.metadata as metadata
import json
import os
from pathlib import Path
import shutil
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOOL_VERSION = '5.4.0'


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save_json(path: Path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def validated_build(receipt: Path):
    from prepare_vault_flash import partition_layout
    build = ROOT / 'artifacts/firmware'
    source = ROOT / 'firmware'
    if not read_json(receipt / 'all.complete.json')['passed']:
        raise ValueError('The selected full build did not pass.')
    result = read_json(receipt / 'build-result.json')
    if not result['passed']:
        raise ValueError('Firmware compilation did not pass.')
    for name in ('source-checks.json', 'models.json'):
        if not read_json(receipt / name)['passed']:
            raise ValueError('Build checks did not pass: ' + name)
    inputs = read_json(receipt / 'build-inputs.json')['files']
    for name, item in inputs.items():
        file = (source / name).resolve()
        if not file.is_relative_to(source.resolve()):
            raise ValueError('Build receipt contains an unsafe input path.')
        if sha(file) != item['sha256']:
            raise ValueError('Build input changed since compilation: ' + name)
    image = build / '01_factory.bin'
    if image.stat().st_size != result['app']['bytes'] or sha(image) != result['app']['sha256']:
        raise ValueError('Firmware does not match its successful build receipt.')
    raw = image.read_bytes()
    if len(raw) < 24 or raw[0] != 0xE9 or int.from_bytes(raw[12:14], 'little') != 9:
        raise ValueError('Firmware is not an ESP32-S3 application image.')
    table = build / 'partition_table/partition-table.bin'
    layout = partition_layout(table.read_bytes())
    app = next(item for item in layout if item[0] == 'factory')
    if app[3:] != (0x20000, 0x950000) or (image.stat().st_size + 4095) // 4096 * 4096 > app[4]:
        raise ValueError('Application image exceeds the supported factory partition.')
    return build, result, len(inputs)


def dependency_distributions():
    from packaging.requirements import Requirement
    pending = [('esptool', set())]
    visited = set()
    distributions = {}
    while pending:
        name, extras = pending.pop()
        key = (name.lower().replace('_', '-'), tuple(sorted(extras)))
        if key in visited:
            continue
        visited.add(key)
        dist = metadata.distribution(name)
        canonical = dist.metadata['Name']
        distributions[canonical] = dist
        for spec in dist.requires or []:
            requirement = Requirement(spec)
            if requirement.marker is not None and not any(
                requirement.marker.evaluate({'extra': extra}) for extra in (extras or {''})
            ):
                continue
            installed = metadata.version(requirement.name)
            if requirement.specifier and installed not in requirement.specifier:
                raise ValueError('Installed dependency does not satisfy ' + spec)
            pending.append((requirement.name, set(requirement.extras)))
    if distributions['esptool'].version != TOOL_VERSION:
        raise ValueError('This bundle currently supports esptool ' + TOOL_VERSION)
    return distributions


def copy_runtime(destination: Path):
    if sys.platform != 'win32' or sys.maxsize <= 2**32 or sys.version_info[:2] != (3, 11):
        raise ValueError('Package with Windows x64 Python 3.11 and esptool 5.4 installed.')
    base = Path(sys.base_prefix).resolve()
    runtime = destination / 'runtime'
    runtime.mkdir()
    for name in ('python.exe', 'python3.dll', 'python311.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
        shutil.copy2(base / name, runtime / name)
    shutil.copy2(base / 'LICENSE.txt', runtime / 'LICENSE-PYTHON.txt')
    dlls = runtime / 'DLLs'
    dlls.mkdir()
    for file in sorted((base / 'DLLs').iterdir()):
        if file.suffix.lower() not in ('.dll', '.pyd'):
            continue
        if file.name.startswith(('_test', '_ctypes_test', 'tcl', 'tk')) or file.name == '_tkinter.pyd':
            continue
        shutil.copy2(file, dlls / file.name)
    excluded = {'site-packages', '__pycache__', 'test', 'tests', 'idlelib', 'tkinter',
                'turtledemo', 'ensurepip', '__phello__'}
    with zipfile.ZipFile(runtime / 'python311.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in sorted((base / 'Lib').rglob('*.py')):
            relative = file.relative_to(base / 'Lib')
            if not any(part in excluded for part in relative.parts):
                archive.write(file, relative.as_posix())
    site = runtime / 'Lib/site-packages'
    site.mkdir(parents=True)
    packages = []
    for name, dist in sorted(dependency_distributions().items()):
        source_site = Path(dist.locate_file('')).resolve()
        licenses = []
        for relative in dist.files or []:
            # Distribution RECORD can include console scripts outside site-packages.
            # Copy package code/metadata only, without another machine's launchers.
            if '..' in relative.parts or '__pycache__' in relative.parts or relative.suffix in ('.pyc', '.pyo', '.pth'):
                continue
            source = Path(dist.locate_file(relative)).resolve()
            if not source.is_relative_to(source_site) or not source.is_file():
                continue
            target = site.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            if any(token in source.name.lower() for token in ('license', 'copying', 'notice')):
                licenses.append(target.relative_to(destination).as_posix())
        packages.append({'name': name, 'version': dist.version, 'licenses': licenses})
    (runtime / 'python311._pth').write_text('python311.zip\nDLLs\nLib\\site-packages\n.\nimport site\n', encoding='ascii')
    save_json(destination / 'runtime-components.json', {
        'python': sys.version.split()[0], 'platform': 'Windows x86-64', 'esptool': TOOL_VERSION,
        'network_install_required': False, 'packages': packages,
    })
    notices = ['# 随包运行环境与许可', '',
               '本包携带当前构建环境的 Python 3.11 Windows x64 运行时，以及 esptool 及其依赖。',
               '没有携带 ESP-IDF SDK、交叉编译器、pip 或本机虚拟环境启动器。',
               'Python 及其随附第三方声明见 runtime/LICENSE-PYTHON.txt；各包许可保留在原 dist-info 目录。',
               '实际版本与许可位置见 runtime-components.json。', '',
               '当前固件保留本项目的既有设备同步身份，应作为本设备升级包保管。',
               '向公众发布固件前，请使用独立身份重新构建，勿将本包误作通用公开固件。', '']
    (destination / 'THIRD-PARTY-NOTICES.md').write_text('\n'.join(notices), encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-run', type=Path, required=True, help='Successful full-build receipt directory')
    parser.add_argument('--output', type=Path, help='New bundle directory (must not already exist)')
    args = parser.parse_args(argv)
    build, result, input_count = validated_build(args.build_run.resolve())
    label = 'yumo-system-update-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + result['app']['sha256'][:8]
    output = (args.output or ROOT / 'artifacts/releases/firmware' / label).resolve()
    if output.exists() or output.with_suffix('.zip').exists():
        raise ValueError('Output already exists; use a new bundle name.')
    output.mkdir(parents=True)
    shutil.copy2(build / '01_factory.bin', output / '01_factory.bin')
    shutil.copy2(build / 'partition_table/partition-table.bin', output / 'partition-table.bin')
    shutil.copy2(ROOT / 'tooling/firmware/portable_flash.py', output / 'portable_flash.py')
    shutil.copy2(ROOT / 'packaging/firmware/Flash.cmd', output / 'Flash.cmd')
    shutil.copy2(ROOT / 'packaging/firmware/esptool.cfg', output / 'esptool.cfg')
    shutil.copy2(ROOT / 'docs/firmware/portable_flash.md', output / 'README.md')
    copy_runtime(output)
    manifest = {
        'schema': 1,
        'product': 'Yumo ESP32-S3-Touch-LCD-1.54',
        'chip': 'esp32s3', 'esptool_version': TOOL_VERSION, 'flash_size': 0x1000000,
        'created': datetime.now().astimezone().isoformat(),
        'app': {'file': '01_factory.bin', 'offset': 0x20000, 'partition_size': 0x950000,
                'bytes': result['app']['bytes'], 'sha256': result['app']['sha256']},
        'partition_table': {'file': 'partition-table.bin', 'offset': 0x8000,
                            'bytes': (output / 'partition-table.bin').stat().st_size,
                            'read_size': 0x1000, 'sha256': sha(output / 'partition-table.bin')},
        'delivery': {'compiled': True, 'flashed': False, 'build_time': result['time'],
                     'verified_build_inputs': input_count},
        'write_policy': 'factory-app-only',
        'preserved': ['bootloader', 'partition-table', 'nvs', 'otadata', 'phy_init', 'assets', 'vault', 'sdcard'],
    }
    save_json(output / 'manifest.json', manifest)
    checksums = {file.relative_to(output).as_posix(): sha(file) for file in sorted(output.rglob('*')) if file.is_file()}
    (output / 'SHA256SUMS').write_text(''.join(value + '  ' + name + '\n' for name, value in checksums.items()), encoding='utf-8')
    archive_path = output.with_suffix('.zip')
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in sorted(output.rglob('*')):
            if file.is_file():
                archive.write(file, output.name + '/' + file.relative_to(output).as_posix())
    Path(str(archive_path) + '.sha256').write_text(sha(archive_path) + '  ' + archive_path.name + '\n', encoding='ascii')
    print(json.dumps({'bundle': str(output), 'zip': str(archive_path), 'zip_bytes': archive_path.stat().st_size,
                      'zip_sha256': sha(archive_path), 'firmware': result['app'],
                      'build_inputs_verified': input_count, 'serial_accessed': False, 'flashed': False}, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit('Package failed: ' + str(error))
