"""Create a separate local playtest; never overwrite an existing installation."""
import argparse
import json
from pathlib import Path
import shutil
import pefile
from patch_client import ADAPTERS,CLIENT_SHA256,ENGINE_SHA256,patch_file,sha256
from make_hud_presets import make as make_hud_presets,validate_ag
from make_game_menu import install as install_menu

PROJECT=Path(__file__).resolve().parents[1]
WORKSPACE=PROJECT.parent
TF_DLL=WORKSPACE/'BXT-Windows/tf/BunnymodXT_tf.dll'
TF_SHA='ee5cc8c32089c31450498cf7ac4cabe5b8ae532a4cde368f4ad4e462c50cd1fa'

def stage(destination: Path,native_dll: Path,settings_from: Path|None=None) -> None:
    source=WORKSPACE/'Half-Life 2005 WON/Half-Life';bundled_bxt=WORKSPACE/'Half-Life 2005 WON/Bunnymod XT'
    destination=destination.resolve();native_dll=native_dll.resolve()
    if destination.exists():raise ValueError('Destination already exists. Choose a new directory; nothing was overwritten.')
    if source.resolve()in destination.parents or destination==source.resolve():raise ValueError('Destination must not be inside the original game')
    original_client=source/'valve_WON/cl_dlls/client.dll'
    if sha256(original_client.read_bytes())!=CLIENT_SHA256:raise ValueError('Original client fingerprint mismatch')
    if sha256((source/'hw.dll').read_bytes())!=ENGINE_SHA256:raise ValueError('Unsupported engine fingerprint')
    native=pefile.PE(str(native_dll));exports={e.name.decode()for e in native.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    if native.FILE_HEADER.Machine!=0x14c or not set(ADAPTERS.values()).issubset(exports):raise ValueError('Build supported Win32 Release adapter first')
    launcher=native_dll.with_name('UltimateHL.exe');dpi=PROJECT/'resources/hl.exe.manifest';defaults=PROJECT/'resources/uhl_defaults.cfg'
    for path in (launcher,dpi,defaults,bundled_bxt/'Injector.exe',TF_DLL,WORKSPACE/'BunnymodXT-Tankfird/LICENSE'):
        if not path.is_file():raise ValueError(f'Missing release input: {path}')
    tf=pefile.PE(str(TF_DLL))
    if tf.FILE_HEADER.Machine!=0x14c or sha256(TF_DLL.read_bytes())!=TF_SHA:raise ValueError('Unverified Tankfird Windows artifact')
    ag=WORKSPACE/'big AG HUD';validate_ag(source,ag)
    destination.mkdir(parents=True,exist_ok=False);game=destination/'Half-Life'
    print(f'Copying baseline game to {game}',flush=True);shutil.copytree(source,game,copy_function=shutil.copy2)
    mod=game/'valve_WON'
    if settings_from and settings_from.is_dir():
        previous=settings_from/'Half-Life/valve_WON'
        for name in ('config.cfg','userconfig.cfg','autoexec.cfg','uhl_preferences.cfg'):
            if(previous/name).is_file():shutil.copy2(previous/name,mod/name)
        if(previous/'SAVE').is_dir():shutil.copytree(previous/'SAVE',mod/'SAVE',dirs_exist_ok=True)
    shutil.copy2(defaults,mod/'uhl_defaults.cfg')
    userconfig=mod/'userconfig.cfg'
    if not userconfig.exists():userconfig.write_bytes(b'')
    for command in (b'exec uhl_defaults.cfg',b'exec uhl_preferences.cfg'):
        if command not in userconfig.read_bytes():
            with userconfig.open('ab')as file:file.write(b'\r\n'+command+b'\r\n')
    preferences=mod/'uhl_preferences.cfg';old=preferences.read_text().splitlines()if preferences.exists()else[]
    forced={'uhl_render_scale':'2','uhl_hud_scale':'1','uhl_hud_texels':'1','gl_spriteblend':'0','uhl_load_fps_min':'0'}
    keep=[line for line in old if not line.split()or line.split()[0].strip('"')not in forced]
    preferences.write_text('\n'.join(keep)+'\n// v3.6 defaults: Big AG art, 2x scene, independent HUD scale\n'+''.join(f'{k} {v}\n'for k,v in forced.items()))
    presets=make_hud_presets(game,destination/'HUD-presets','ag',ag)
    install_menu(game)
    shutil.copy2(launcher,destination/'UltimateHL.exe')
    client=mod/'cl_dlls/client.dll';stock=client.with_name('client.stock.dll')
    if stock.exists():raise ValueError('Unexpected stock backup in source')
    client.rename(stock);patched=patch_file(stock,client)
    shutil.copy2(native_dll,game/'UHLCompat.dll');shutil.copy2(dpi,game/'hl.exe.manifest')
    bxt=destination/'BXT';bxt.mkdir();shutil.copy2(bundled_bxt/'Injector.exe',bxt/'Injector.exe')
    shutil.copy2(TF_DLL,bxt/'BunnymodXT.dll');shutil.copy2(WORKSPACE/'BunnymodXT-Tankfird/LICENSE',bxt/'LICENSE')
    shutil.copy2(WORKSPACE/'BunnymodXT-Tankfird/README.md',bxt/'Tankfird-features.txt')
    common='@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
    for name,options in (('START HERE.bat',''),('Play - windowed control.bat',' --windowed')):
        (destination/name).write_bytes((common+'"%~dp0UltimateHL.exe"'+options+'\r\nif errorlevel 1 pause\r\n').encode('ascii'))
    (destination/'START_HERE.txt').write_text(
        'UltimateHL v3.6 - experimental local build, not run-certified\n\n'
        'START HERE.bat: Big AG HUD art + 2x scene + Tankfird BXT + default OBS-capable DXGI output.\n'
        'Play - windowed control.bat: retained ordinary window, with BXT.\n'
        'No OBS-safe, HUD-selector or redundant quality BAT files are needed.\n'
        'Use the in-game UltimateHL settings panel for 3D quality, art and HUD scale.\n'
        'HUD art: Big AG default; Original 640 / Compact 320 remain optional (restart for art).\n'
        'HUD scale: 1x / 1.5x / 2x, separate premultiplied bilinear filtering, live.\n'
        'Console: uhl_hud_scale 1, 1.5 or 2; uhl_render_scale 1 or 2.\n'
        'Load FPS floor remains 0 by default for glitches.\n'
        'Local SP sv_cheats 1 now synchronizes BOTH engine and GameDLL impulse caches.\n'
        'No automatic sv_cheats enable; no remote/multiplayer/coop authority bypass.\n'
        'Tankfird feature list is BXT/Tankfird-features.txt.\n'
        'Do not stack RInput or use this on VAC. Existing saves/configs were copied.\n'
        'Windows EPP/pointer speed, OBS profiles and global GPU settings are untouched.\n'
        'Older installations remain available for rollback. See README.md and V36-FINDINGS.md.\n')
    shutil.copy2(PROJECT/'resources/ABOUT AND FEATURES.txt',destination/'ABOUT AND FEATURES.txt')
    for name in ('README.md','TESTING.md'):
        if(PROJECT/name).exists():shutil.copy2(PROJECT/name,destination/name)
    findings=PROJECT/'reports/V36-FINDINGS.md'
    if findings.exists():shutil.copy2(findings,destination/findings.name)
    hashes={}
    for relative in ('hl.exe','hw.dll','valve_WON/dlls/hl.dll','valve_WON/WON.cfg'):
        before=sha256((source/relative).read_bytes());after=sha256((game/relative).read_bytes())
        if before!=after:raise RuntimeError(f'Protected baseline changed: {relative}')
        hashes[relative]=after
    manifest=dict(experimental=True,adapter_version=3,build_version='3.6',engine_binary_modified=False,
        engine_command_slots_wrapped=True,renderer_operands_patched_runtime=True,hud_preset_initial='ag',hud_preset_lists=presets,
        hud_scale_default=1,loading_fps_floor_default=0,render_scale_default=2,load_menu_default=0,cursor_lock_default=1,
        load_mouse_freeze_default=1,msaa_rasterization_default=0,hud_pixels_default=1,normal_dxgi_present_default=1,
        immediate_local_cheats_default=1,client=patched,unchanged_baseline_files=hashes,
        native_sha256=sha256(native_dll.read_bytes()),launcher_sha256=sha256(launcher.read_bytes()),
        dpi_manifest_sha256=sha256(dpi.read_bytes()),render_defaults_sha256=sha256(defaults.read_bytes()),
        settings_copied_from=str(settings_from)if settings_from else None,bxt_sha256=TF_SHA,bxt_source=str(TF_DLL),
        bxt_features_source=str(WORKSPACE/'BunnymodXT-Tankfird'),ag_source=str(ag),
        implemented=['full-resolution GPU DXGI output','scene-only 2x default','Big AG HUD default, stock 640 and compact presets',
            'independent 1x/1.5x/2x premultiplied bilinear HUD canvas','Tankfird Windows BXT artifact and feature list',
            'local-SP engine AND GameDLL impulse cheat cache sync','load/changelevel input/cursor protection; default load FPS floor 0',
            'two launcher BAT files; settings in game','Windows mouse preference writes blocked'],
        not_implemented=['arbitrary continuous HUD scales','run/category certification','remote/multiplayer cheat permission changes','quicksave backup changes'])
    (destination/'stage-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Prepared {destination}; game not launched.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--destination',type=Path,default=PROJECT/'playtest-v3.6')
    parser.add_argument('--settings-from',type=Path,default=PROJECT/'playtest-v3.5')
    parser.add_argument('--native-dll',type=Path,default=PROJECT/'build/Release/UHLCompat.dll')
    args=parser.parse_args();stage(args.destination,args.native_dll,args.settings_from)
