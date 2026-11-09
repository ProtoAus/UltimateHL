"""Build a NEW local install from the user's licensed 3248/WON game and AG art.

The public add-on contains no Valve game files, HUD artwork or BXT injector.
Nothing is downloaded, launched or overwritten in an existing installation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

import pefile
from patch_client import CLIENT_SHA256, ENGINE_SHA256
from make_hud_presets import make as make_hud_presets, validate_ag
from make_game_menu import install as install_menu
from text_quality import install as install_fonts
from package_distribution import keep_game_file

PROJECT = Path(__file__).resolve().parents[1]
VERSION = '3.7.1'


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_inputs(game: Path, binaries: Path, bxt: Path, injector: Path, ag: Path) -> None:
    for rel, expected in [('hw.dll', ENGINE_SHA256), ('valve_WON/cl_dlls/client.dll', CLIENT_SHA256)]:
        if sha(game / rel) != expected:
            raise ValueError(f'Unsupported {rel}: this add-on requires the stock 2005 build 3248/WON files')
    for path in [game / 'hl.exe', binaries / 'UHLCompat.dll', binaries / 'UltimateHL.exe', bxt, injector]:
        if not path.is_file() or pefile.PE(str(path), fast_load=True).FILE_HEADER.Machine != 0x14c:
            raise ValueError(f'Missing or non-Win32 executable: {path}')
    adapter = pefile.PE(str(binaries / 'UHLCompat.dll'))
    exports = {e.name for e in adapter.DIRECTORY_ENTRY_EXPORT.symbols}
    if b'UHL_AttachClient' not in exports or b'UHL_AttachClient\0' not in bxt.read_bytes():
        raise ValueError('Use the UltimateHL adapter and rebuilt Tankfird BXT supplied in the same release')
    validate_ag(game, ag)


def install(game: Path, destination: Path, binaries: Path, bxt: Path, injector: Path, ag: Path) -> None:
    game, destination = game.resolve(), destination.resolve()
    if destination.exists():
        raise ValueError('Destination already exists. Choose a NEW folder; nothing was overwritten.')
    if game == destination or game in destination.parents or destination in game.parents:
        raise ValueError('Game and destination must be separate, non-nested folders')
    for supplied in (binaries, bxt, injector, ag):
        if destination == supplied.resolve() or destination in supplied.resolve().parents:
            raise ValueError('Destination must not contain any input')
    validate_inputs(game, binaries, bxt, injector, ag)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.uhl-install-', dir=destination.parent) as temp:
        target = Path(temp) / 'install'
        out_game = target / 'Half-Life'
        out_game.mkdir(parents=True)
        # Copy only runtime files. Do not clone saves, personal configs, user
        # platform state, diagnostics or symlinks into the clean installation.
        for path in sorted(game.rglob('*')):
            if path.is_symlink():
                raise ValueError(f'Refusing linked game content: {path}')
            if path.is_file() and keep_game_file(path.relative_to(game)):
                out = out_game / path.relative_to(game)
                out.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, out)
        mod = out_game / 'valve_WON'
        for resource, name in [('distribution-config.cfg', 'config.cfg'),
                               ('distribution-userconfig.cfg', 'userconfig.cfg'),
                               ('distribution-preferences.cfg', 'uhl_preferences.cfg'),
                               ('uhl_defaults.cfg', 'uhl_defaults.cfg')]:
            shutil.copy2(PROJECT / 'resources' / resource, mod / name)
        (mod / 'autoexec.cfg').write_text('// Optional local overrides.\n', encoding='ascii')
        make_hud_presets(out_game, target / 'HUD-presets', 'ag', ag)
        # Drop diagnostic paths from the generated preset manifest.
        (target / 'HUD-presets/presets.json').unlink()
        install_menu(out_game)
        install_fonts(out_game)
        shutil.copy2(binaries / 'UHLCompat.dll', out_game / 'UHLCompat.dll')
        shutil.copy2(binaries / 'UltimateHL.exe', target / 'UltimateHL.exe')
        shutil.copy2(PROJECT / 'resources/hl.exe.manifest', out_game / 'hl.exe.manifest')
        out_bxt = target / 'BXT'
        out_bxt.mkdir()
        shutil.copy2(bxt, out_bxt / 'BunnymodXT.dll')
        shutil.copy2(injector, out_bxt / 'Injector.exe')
        shutil.copy2(PROJECT / 'third_party/BunnymodXT-LICENSE.txt', out_bxt / 'LICENSE')
        shutil.copy2(PROJECT / 'third_party/Tankfird-features.md', out_bxt / 'Tankfird-features.txt')
        shutil.copy2(PROJECT / 'README.md', target / 'README.md')
        shutil.copy2(PROJECT / 'resources/ABOUT AND FEATURES.txt', target / 'ABOUT AND FEATURES.txt')
        for name, options in [('START HERE.bat', ''), ('Play - windowed control.bat', ' --windowed')]:
            (target / name).write_bytes(('@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
                                        '"%~dp0UltimateHL.exe"' + options + '\r\nif errorlevel 1 pause\r\n').encode('ascii'))
        protected = {rel: sha(out_game / rel) for rel in ['hl.exe', 'hw.dll', 'valve_WON/cl_dlls/client.dll',
                                                        'valve_WON/dlls/hl.dll', 'valve_WON/WON.cfg']}
        for rel, expected in protected.items():
            if sha(game / rel) != expected:
                raise ValueError(f'Protected game file changed while installing: {rel}')
        manifest = dict(build_version=VERSION, adapter_version=3, experimental=True,
                        client_layout='stock', engine_binary_modified=False, runtime_engine_hooks=True,
                        fractional_mouse_precision=True, hud_preset_initial='ag', hud_scale_default=1,
                        render_scale_default=2, loading_fps_floor_default=0,
                        personal_configs_included=False, personal_saves_included=False,
                        unchanged_baseline_files=protected, native_sha256=sha(out_game / 'UHLCompat.dll'),
                        launcher_sha256=sha(target / 'UltimateHL.exe'), bxt_sha256=sha(out_bxt / 'BunnymodXT.dll'))
        (target / 'stage-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        files = sorted(p for p in target.rglob('*') if p.is_file())
        (target / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(target).as_posix()}\n' for p in files), encoding='utf-8')
        if destination.exists():
            raise ValueError('Destination appeared during installation; nothing overwritten')
        os.rename(target, destination)
    print(f'Installed UltimateHL {VERSION}: {destination}\nRun UltimateHL.exe. Your original game, saves and configs were not changed.')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True, help='supported Half-Life folder containing hl.exe')
    parser.add_argument('--destination', type=Path, required=True, help='NEW output folder')
    parser.add_argument('--binaries', type=Path, default=PROJECT / 'bin')
    parser.add_argument('--bxt-dll', type=Path, help='defaults to bin/BunnymodXT.dll')
    parser.add_argument('--injector', type=Path, required=True, help='your BXT Injector.exe')
    parser.add_argument('--ag-hud', type=Path, required=True, help='your Big AG HUD folder (hud.txt and .spr files)')
    args = parser.parse_args()
    install(args.game, args.destination, args.binaries, args.bxt_dll or args.binaries / 'BunnymodXT.dll', args.injector, args.ag_hud)


if __name__ == '__main__':
    main()
