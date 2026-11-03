"""Package current SD resources without depending on an earlier release ZIP."""
from pathlib import Path
import argparse
import zipfile

SD_ROOT = Path(__file__).resolve().parents[2] / 'resources' / 'sdcard'


def write_chiptune_archive(sd_root, archive):
    source = Path(sd_root) / 'chiptune'
    files = sorted(path for path in source.iterdir() if path.is_file())
    target = Path(archive)
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for path in files:
            bundle.write(path, 'chiptune/' + path.name)


def write_template_archive(sd_root, archive):
    source = Path(sd_root)
    directories = ('config', 'recordings', 'nes')
    names = ('vault.json', 'nes-keys.json')
    for name in directories:
        if not (source / name).is_dir():
            raise FileNotFoundError(source / name)
    # Only the two configuration templates belong in this package. Never add
    # recordings, ROMs, save files or other user-created SD contents.
    contents = [(name, (source / 'config' / name).read_bytes()) for name in names]
    target = Path(archive)
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for name in directories:
            bundle.writestr(name + '/', b'')
        for name, data in contents:
            bundle.writestr('config/' + name, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('template', 'chiptune'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    writer = write_template_archive if args.kind == 'template' else write_chiptune_archive
    writer(SD_ROOT, args.output)
    print(args.output.resolve())


if __name__ == '__main__':
    main()
