"""Build a NEW, AG-default runtime bundle; never prune a user's installation in place."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import zipfile

import pefile
from client_layout import ATTACH_MARKER
from patch_client import ADAPTERS, CLIENT_SHA256, ENGINE_SHA256
from stage_playtest import TF_SHA

PROJECT = Path(__file__).resolve().parents[1]
BUILD_VERSION = '3.7'  # of new packages; sources are staged v3.6 copies
CLIENT = 'Half-Life/valve_WON/cl_dlls/client.dll'
GAME_DIRS = {'gldrv', 'platform', 'reslists', 'valve', 'valve_won'}
JUNK_SUFFIXES = {'.original', '.bak', '.old', '.pdb', '.ilk', '.obj', '.lib', '.exp',
                 '.pyc', '.log', '.tsv', '.dem', '.sav', '.tmp', '.dmp', '.so', '.hpk'}
JUNK_DIRS = {'save', 'saves', 'logs', 'screenshots', '__pycache__', 'adapter-backups'}
BASE_CONFIGS = {'default.cfg', 'language.cfg', 'skill.cfg', 'violence.cfg'}
ROOT_FILES = {'hl.exe', 'hl.exe.manifest', 'language.inf', 'steam_appid.txt', 'readme.txt',
              'a3dapi.dll', 'core.dll', 'dbg.dll', 'demoplayer.dll', 'filesystem_steam.dll',
              'hw.dll', 'mp3dec.asi', 'mss32.dll', 'mssv12.asi', 'mssv29.asi', 'steam.dll',
              'steam_api.dll', 'steam_api_c.dll', 'steamclient.dll', 'sw.dll', 'uhlcompat.dll',
              'vgui.dll', 'vgui2.dll', 'voice_miles.dll', 'voice_speex.dll'}
PROTECTED = ('hl.exe', 'hw.dll', 'valve_WON/dlls/hl.dll', 'valve_WON/WON.cfg')
CROSSHAIR_STYLES = ('original', 'ultimatehl')  # selectable in the settings panel
DEFAULT_CROSSHAIRS = 'HUD-presets/crosshairs/ultimatehl/crosshairs.spr'
FONT_STYLES = ('original', 'smooth')  # engine text font, selectable in the settings panel
DEFAULT_FONT = 'HUD-presets/fonts/smooth/TrackerScheme.res'
LIVE_FONT = 'Half-Life/platform/resource/TrackerScheme.res'
RESOURCE_FILES = {'distribution-config.cfg': 'Half-Life/valve_WON/config.cfg',
                  'distribution-userconfig.cfg': 'Half-Life/valve_WON/userconfig.cfg',
                  'distribution-preferences.cfg': 'Half-Life/valve_WON/uhl_preferences.cfg',
                  'uhl_defaults.cfg': 'Half-Life/valve_WON/uhl_defaults.cfg',
                  'distribution-start.txt': 'START_HERE.txt',
                  'ABOUT AND FEATURES.txt': 'ABOUT AND FEATURES.txt'}


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def keep_game_file(relative: Path) -> bool:
    """Conservative file policy: never blanket-delete BMPs/TXTs used as assets."""
    parts = tuple(part.lower() for part in relative.parts)
    if not parts or relative.is_absolute() or '..' in parts:
        return False
    name = parts[-1]
    suffix = Path(name).suffix
    if any(part in JUNK_DIRS for part in parts) or suffix in JUNK_SUFFIXES:
        return False
    if name.startswith('uhl-') or name in {'client.stock.dll', 'tempdecal.wad'}:
        return False
    if len(parts) == 1:
        return name in ROOT_FILES
    if parts[0] not in GAME_DIRS:
        return False
    if parts[:2] == ('platform', 'config') or parts == ('platform', 'friends', 'servers.vdf'):
        return False  # cached user/friends/server-browser state, not GUI resources
    if len(parts) == 2 and parts[0] in {'valve', 'valve_won'} and suffix == '.cfg':
        return name in (BASE_CONFIGS if parts[0] == 'valve' else {'won.cfg', 'uhl_defaults.cfg'})
    return True


def planned_files(source: Path) -> tuple[list[Path], list[Path]]:
    kept, excluded = [], []
    for path in sorted(source.rglob('*')):
        if not path.is_file():
            continue
        if path.is_symlink() or not path.resolve().is_relative_to(source.resolve()):
            raise ValueError(f'Refusing linked payload: {path}')
        rel = path.relative_to(source)
        parts = rel.parts
        allowed = (parts[0] == 'Half-Life' and keep_game_file(Path(*parts[1:]))) or (
            len(parts) == 3 and parts[0] == 'HUD-presets' and parts[1] in ('stock', 'compact', 'ag') and path.suffix == '.txt') or (
            len(parts) == 4 and parts[:2] == ('HUD-presets', 'crosshairs') and parts[2] in CROSSHAIR_STYLES and parts[3] == 'crosshairs.spr') or (
            len(parts) == 4 and parts[:2] == ('HUD-presets', 'fonts') and parts[2] in FONT_STYLES and parts[3] == 'TrackerScheme.res')
        if allowed:
            kept.append(path)
        else:
            excluded.append(path)
    return kept, excluded


def copy_payload(source: Path, target: Path, native: Path, launcher: Path,
                 bxt_dll: Path | None = None, stock_client: Path | None = None) -> dict:
    """bxt_dll/stock_client give the legit layout: the UltimateHL BXT and the unmodified client."""
    kept, excluded = planned_files(source)
    for path in kept:
        dest = target / path.relative_to(source)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest)
    bxt = target / 'BXT'
    bxt.mkdir(parents=True)
    for name in ('Injector.exe', 'BunnymodXT.dll', 'LICENSE'):
        shutil.copy2(source / 'BXT' / name, bxt / name)
    if bxt_dll:
        shutil.copy2(bxt_dll, bxt / 'BunnymodXT.dll')
    if stock_client:
        shutil.copy2(stock_client, target / CLIENT)
    feature_notes = source / 'BXT/Tankfird-features.txt'
    if not feature_notes.is_file():
        feature_notes = source / 'BXT/Tankfird-features.md'
    shutil.copy2(feature_notes, bxt / 'Tankfird-features.txt')
    shutil.copy2(native, target / 'Half-Life/UHLCompat.dll')
    shutil.copy2(launcher, target / 'UltimateHL.exe')
    for resource, relative in RESOURCE_FILES.items():
        shutil.copy2(PROJECT / 'resources' / resource, target / relative)
    (target / 'Half-Life/valve_WON/autoexec.cfg').write_text('// Optional local overrides; intentionally empty.\n', encoding='ascii')
    # Installing the entire AG preset also resets any previous weapon-list override.
    for path in (target / 'HUD-presets/ag').glob('*.txt'):
        shutil.copy2(path, target / 'Half-Life/valve_WON/sprites' / path.name)
    # Likewise start on the UltimateHL crosshairs, whichever style the source copy had selected.
    if (target / DEFAULT_CROSSHAIRS).is_file():
        shutil.copy2(target / DEFAULT_CROSSHAIRS, target / 'Half-Life/valve_WON/sprites/crosshairs.spr')
    # And on smooth engine text.
    if (target / DEFAULT_FONT).is_file():
        shutil.copy2(target / DEFAULT_FONT, target / LIVE_FONT)
    common = '@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
    for name, options in (('START HERE.bat', ''), ('Play - windowed control.bat', ' --windowed')):
        (target / name).write_bytes((common + '"%~dp0UltimateHL.exe"' + options + '\r\nif errorlevel 1 pause\r\n').encode('ascii'))
    return dict(excluded_files=[p.relative_to(source).as_posix() for p in excluded],
                excluded_source_bytes=sum(p.stat().st_size for p in excluded),
                source_files=len(kept) + len(excluded),
                source_bytes=sum(p.stat().st_size for p in kept + excluded))


def validate_source(source: Path, native: Path, launcher: Path,
                    bxt_dll: Path | None = None, stock_client: Path | None = None) -> dict:
    if (bxt_dll is None) != (stock_client is None):
        raise ValueError('The legit layout needs both the UltimateHL BXT and the stock client')
    if bxt_dll is not None:
        if ATTACH_MARKER not in bxt_dll.read_bytes():
            raise ValueError('This BXT cannot attach UltimateHL; use the BXT built from BunnymodXT-Tankfird')
        if sha(stock_client) != CLIENT_SHA256:
            raise ValueError('Not the unmodified valve_WON client.dll')
    manifest = json.loads((source / 'stage-manifest.json').read_text())
    if manifest.get('build_version') != '3.6' or manifest.get('adapter_version') != 3:
        raise ValueError('Only a verified v3.6 staged source is supported')
    checks = {'Half-Life/UHLCompat.dll': manifest['native_sha256'],
              'UltimateHL.exe': manifest['launcher_sha256'],
              'BXT/BunnymodXT.dll': TF_SHA,
              'Half-Life/valve_WON/cl_dlls/client.dll': manifest['client']['patched_sha256']}
    checks.update({'Half-Life/' + rel: manifest['unchanged_baseline_files'][rel] for rel in PROTECTED})
    for relative, expected in checks.items():
        if sha(source / relative) != expected:
            raise ValueError(f'Changed release input: {relative}')
    if sha(source / 'Half-Life/hw.dll') != ENGINE_SHA256 or manifest['client']['source_sha256'] != CLIENT_SHA256:
        raise ValueError('Unsupported baseline fingerprints')
    image = pefile.PE(str(native))
    exports = {entry.name.decode('ascii') for entry in image.DIRECTORY_ENTRY_EXPORT.symbols if entry.name}
    if image.FILE_HEADER.Machine != 0x14c or not set(ADAPTERS.values()).issubset(exports):
        raise ValueError('Unsupported native adapter binary')
    if bxt_dll is not None and 'UHL_AttachClient' not in exports:
        raise ValueError('This adapter cannot attach to the unmodified client')
    if pefile.PE(str(launcher)).FILE_HEADER.Machine != 0x14c:
        raise ValueError('Launcher must be Win32')
    for preset in ('ag', 'stock', 'compact'):
        if not (source / 'HUD-presets' / preset / 'hud.txt').is_file():
            raise ValueError(f'Missing {preset} HUD preset')
    return manifest


def write_manifest(target: Path, original: dict, legit: bool = False) -> None:
    manifest = dict(experimental=True, adapter_version=3, build_version=BUILD_VERSION,
        distribution_edition='AG-clean', hud_preset_initial='ag', hud_scale_default=1,
        crosshair_style_initial='ultimatehl' if (target / DEFAULT_CROSSHAIRS).is_file() else 'original',
        text_style_initial='smooth' if (target / DEFAULT_FONT).is_file() else 'original',
        client_layout='stock' if legit else 'patched', client_sha256=sha(target / CLIENT),
        bxt_build='UltimateHL (BunnymodXT-Tankfird source)' if legit else 'supplied Tankfird DLL',
        render_scale_default=2, loading_fps_floor_default=0, normal_dxgi_present_default=1,
        personal_configs_included=False, personal_saves_included=False,
        engine_binary_modified=False, runtime_engine_hooks=True,
        client=original['client'], unchanged_baseline_files=original['unchanged_baseline_files'],
        native_sha256=sha(target / 'Half-Life/UHLCompat.dll'), launcher_sha256=sha(target / 'UltimateHL.exe'),
        bxt_sha256=sha(target / 'BXT/BunnymodXT.dll'),
        dpi_manifest_sha256=sha(target / 'Half-Life/hl.exe.manifest'),
        render_defaults_sha256=sha(target / 'Half-Life/valve_WON/uhl_defaults.cfg'))
    (target / 'stage-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    files = sorted(p for p in target.rglob('*') if p.is_file())
    (target / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(target).as_posix()}\n' for p in files), encoding='utf-8')


def verify_distribution(target: Path) -> None:
    manifest = json.loads((target / 'stage-manifest.json').read_text())
    if manifest['hud_preset_initial'] != 'ag':
        raise ValueError('AG must be the initial art preset')
    for rel, expected in manifest['unchanged_baseline_files'].items():
        if sha(target / 'Half-Life' / rel) != expected:
            raise ValueError(f'Protected game file changed: {rel}')
    for path in (target / 'HUD-presets/ag').glob('*.txt'):
        if path.read_bytes() != (target / 'Half-Life/valve_WON/sprites' / path.name).read_bytes():
            raise ValueError(f'AG preset not installed: {path.name}')
    if manifest.get('crosshair_style_initial') == 'ultimatehl':
        live = target / 'Half-Life/valve_WON/sprites/crosshairs.spr'
        if not all((target / 'HUD-presets/crosshairs' / style / 'crosshairs.spr').is_file() for style in CROSSHAIR_STYLES):
            raise ValueError('Crosshair style presets are incomplete')
        if not live.is_file() or live.read_bytes() != (target / DEFAULT_CROSSHAIRS).read_bytes():
            raise ValueError('UltimateHL crosshair style not installed')
    if manifest.get('text_style_initial') == 'smooth':
        if not all((target / 'HUD-presets/fonts' / style / 'TrackerScheme.res').is_file() for style in FONT_STYLES):
            raise ValueError('Engine font presets are incomplete')
        if (target / LIVE_FONT).read_bytes() != (target / DEFAULT_FONT).read_bytes():
            raise ValueError('Smooth engine text not installed')
    if manifest.get('client_layout') == 'stock':
        if sha(target / CLIENT) != CLIENT_SHA256:
            raise ValueError('The legit layout needs the unmodified client.dll')
        if ATTACH_MARKER not in (target / 'BXT/BunnymodXT.dll').read_bytes():
            raise ValueError('The legit layout needs the UltimateHL BXT')
    if sorted(p.name for p in target.glob('*.bat')) != ['Play - windowed control.bat', 'START HERE.bat']:
        raise ValueError('Expected exactly two launchers')
    clean_configs = {Path(rel).relative_to('Half-Life').as_posix().lower() for rel in RESOURCE_FILES.values() if rel.startswith('Half-Life/')}
    clean_configs.add('valve_won/autoexec.cfg')
    for path in (target / 'Half-Life').rglob('*'):
        if path.is_file():
            rel = path.relative_to(target / 'Half-Life')
            if not keep_game_file(rel) and rel.as_posix().lower() not in clean_configs:
                raise ValueError(f'Non-runtime file leaked into distribution: {rel}')
    expected_files = set()
    for line in (target / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        expected, rel = line.split('  ', 1)
        expected_files.add(rel)
        if sha(target / rel) != expected:
            raise ValueError(f'Package integrity mismatch: {rel}')
    actual_files = {p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file() and p.name != 'SHA256SUMS.txt'}
    if actual_files != expected_files:
        raise ValueError('Untracked package files or an incomplete hash list')


def package(source: Path, destination: Path, native: Path, launcher: Path, archive: Path | None = None,
            bxt_dll: Path | None = None, stock_client: Path | None = None) -> dict:
    source, destination = source.resolve(), destination.resolve()
    if destination.exists() or (archive and archive.exists()):
        raise ValueError('Choose NEW output paths; no existing installation/archive is overwritten')
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Output and source must be separate, non-nested directories')
    if archive and (destination == archive.resolve() or destination in archive.resolve().parents):
        raise ValueError('Archive must be outside the runtime folder')
    original = validate_source(source, native, launcher, bxt_dll, stock_client)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.package-', dir=destination.parent) as temp:
        target = Path(temp) / destination.name
        target.mkdir()
        report = copy_payload(source, target, native, launcher, bxt_dll, stock_client)
        write_manifest(target, original, legit=bxt_dll is not None)
        verify_distribution(target)
        if destination.exists():
            raise ValueError('Output appeared during packaging; nothing overwritten')
        os.rename(target, destination)
    files = sorted(p for p in destination.rglob('*') if p.is_file())
    report.update(packaged_files=len(files), packaged_bytes=sum(p.stat().st_size for p in files),
                  removed_bytes_net=report['source_bytes'] - sum(p.stat().st_size for p in files),
                  default_hud='ag', source_untouched=True)
    if archive:
        archive.parent.mkdir(parents=True, exist_ok=True)
        with archive.open('xb') as stream:
            with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6, strict_timestamps=False) as zipped:
                for path in files:
                    zipped.write(path, destination.name + '/' + path.relative_to(destination).as_posix())
        with zipfile.ZipFile(archive) as zipped:
            if zipped.testzip() is not None:
                raise ValueError('Archive CRC verification failed')
        report.update(archive_bytes=archive.stat().st_size, archive_sha256=sha(archive))
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=PROJECT / 'playtest-v3.6')
    parser.add_argument('--destination', type=Path, default=PROJECT / f'dist/UltimateHL-v{BUILD_VERSION}')
    parser.add_argument('--native-dll', type=Path, default=PROJECT / 'build/Release/UHLCompat.dll')
    parser.add_argument('--launcher', type=Path, default=PROJECT / 'build/Release/UltimateHL.exe')
    parser.add_argument('--bxt-dll', type=Path, default=PROJECT.parent / 'BunnymodXT-Tankfird/build/Release/BunnymodXT.dll',
                        help='UltimateHL BXT for the legit layout')
    parser.add_argument('--stock-client', type=Path,
                        default=PROJECT.parent / 'Half-Life 2005 WON/Half-Life/valve_WON/cl_dlls/client.dll',
                        help='unmodified valve_WON client.dll for the legit layout')
    parser.add_argument('--patched-client', action='store_true',
                        help='old layout: client.dll patched on disk and the supplied official BXT')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.report and args.report.exists():
        raise ValueError('Choose a NEW cleanup report path')
    if args.report and args.destination.resolve() in args.report.resolve().parents:
        raise ValueError('Development cleanup report must stay outside the runtime folder')
    legit = not args.patched_client
    report = package(args.source, args.destination, args.native_dll, args.launcher, args.archive,
                     args.bxt_dll if legit else None, args.stock_client if legit else None)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'excluded_files'}, indent=2))
    print('Runtime folder:', args.destination.resolve())
    if args.archive:
        print('Archive:', args.archive.resolve())


if __name__ == '__main__':
    main()
