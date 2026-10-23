"""Validate an activated external toolchain; never rewrite its virtual environment."""
from pathlib import Path
import os, re, sys
from environment import ROOT, sdk_path, executable

BUILD = ROOT / 'artifacts/firmware'

def main():
    from apply_sdk_patch import check
    check(sdk_path())
    for name in ('cmake', 'ninja', 'xtensa-esp-elf-gcc'):
        executable(name)
    if sys.version_info < (3, 11):
        raise RuntimeError('Python 3.11 or newer is required.')
    print('External ESP-IDF toolchain is available.')

def prepare_build():
    cache = BUILD / 'CMakeCache.txt'
    if not cache.is_file():
        return
    values = dict(re.findall(r'^([A-Za-z_][A-Za-z0-9_.-]*):[^=\r\n]+=([^\r\n]*)$',
                            cache.read_text(encoding='utf-8', errors='replace'), re.M))
    for key, expected in [('CMAKE_HOME_DIRECTORY', ROOT/'firmware'),
                          ('CMAKE_CACHEFILE_DIR', BUILD), ('PYTHON', Path(sys.executable))]:
        if not values.get(key) or Path(values[key]).resolve() != expected.resolve():
            raise RuntimeError('Stale build cache after relocation. Remove artifacts/firmware and rebuild.')

if __name__ == '__main__':
    main()
