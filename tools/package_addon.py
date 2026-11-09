"""Create an allowlisted public ZIP: never package a staged/installed game."""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import zipfile

import pefile

PROJECT = Path(__file__).resolve().parents[1]
VERSION = '3.7.1'
ROOT_FILES = ('README.md', 'LICENSE', 'CHANGELOG.md', 'TESTING.md', 'requirements.txt')
TOOLS = ('install_addon.py', 'patch_client.py', 'make_hud_presets.py', 'make_game_menu.py',
         'text_quality.py', 'package_distribution.py', 'client_layout.py', 'stage_playtest.py')
RESOURCES = ('distribution-config.cfg', 'distribution-userconfig.cfg', 'distribution-preferences.cfg',
             'uhl_defaults.cfg', 'hl.exe.manifest', 'ABOUT AND FEATURES.txt')
THIRD_PARTY = ('README.md', 'BunnymodXT-LICENSE.txt', 'BunnymodXT-uhl.patch', 'Tankfird-features.md')


def payload(binaries: Path, bxt: Path) -> dict[str, bytes]:
    files = {name: (PROJECT / name).read_bytes() for name in ROOT_FILES}
    for directory, names in [('tools', TOOLS), ('resources', RESOURCES), ('third_party', THIRD_PARTY)]:
        files.update({f'{directory}/{name}': (PROJECT / directory / name).read_bytes() for name in names})
    for name, path in [('UHLCompat.dll', binaries / 'UHLCompat.dll'),
                       ('UltimateHL.exe', binaries / 'UltimateHL.exe'), ('BunnymodXT.dll', bxt)]:
        data = path.read_bytes()
        if pefile.PE(data=data, fast_load=True).FILE_HEADER.Machine != 0x14c:
            raise ValueError(f'Not a Win32 binary: {path}')
        files['bin/' + name] = data
    if b'UHL_AttachClient\0' not in files['bin/BunnymodXT.dll']:
        raise ValueError('BXT must be the UltimateHL-integrated build')
    # Only the explicit files above are ever read, never rglob a game/dist folder.
    return files


def package(archive: Path, binaries: Path, bxt: Path) -> None:
    files = payload(binaries, bxt)
    files['SHA256SUMS.txt'] = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n'
                                      for name, data in sorted(files.items())).encode('utf-8')
    archive.parent.mkdir(parents=True, exist_ok=True)
    with archive.open('xb') as out:
        with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
            for name, data in sorted(files.items()):
                zipped.writestr(f'UltimateHL-v{VERSION}-addon/{name}', data)
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() is not None:
            raise ValueError('ZIP integrity test failed')
    print(f'Public add-on: {archive} ({len(files)} files; no game, HUD assets or injector)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True, help='NEW ZIP path')
    parser.add_argument('--binaries', type=Path, default=PROJECT / 'build/Release')
    parser.add_argument('--bxt-dll', type=Path, required=True)
    args = parser.parse_args()
    package(args.archive, args.binaries, args.bxt_dll)
