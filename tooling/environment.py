"""Locate user-installed tools. No downloads, registry changes or private paths."""
from pathlib import Path
import os
import shutil

ROOT = Path(__file__).resolve().parents[1]

def executable(name, variable=None):
    value = os.environ.get(variable, '') if variable else ''
    found = shutil.which(value or name)
    if not found:
        raise RuntimeError(f'Install {name} and add it to PATH' +
                           (f', or set {variable}.' if variable else '.'))
    return Path(found).resolve()

def sdk_path():
    value = os.environ.get('IDF_PATH')
    if not value:
        raise RuntimeError('Activate ESP-IDF 6.1 first (see docs/build.md).')
    sdk = Path(value).resolve()
    if not (sdk / 'tools/cmake/project.cmake').is_file():
        raise RuntimeError('IDF_PATH does not contain ESP-IDF.')
    return sdk

def java_tool(name='java'):
    base = os.environ.get('JAVA_HOME')
    if base:
        tool = Path(base) / 'bin' / (name + ('.exe' if os.name == 'nt' else ''))
        if tool.is_file():
            return tool.resolve()
        raise RuntimeError(f'JAVA_HOME does not contain {name}.')
    return executable(name)
