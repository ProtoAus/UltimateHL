import json
from pathlib import Path
import sys
import tempfile
import unittest

from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_distribution as pd
from package_distribution import CLIENT, copy_payload, keep_game_file, package, sha, verify_distribution, write_manifest


class DistributionTests(unittest.TestCase):
    def test_assets_remain_but_development_and_personal_state_do_not(self):
        for name in ('hl.exe', 'hw.dll', 'DemoPlayer.dll', 'UHLCompat.dll', 'valve/maps/c1a0.bsp',
                     'valve/gfx/shell/logo.bmp', 'valve/sprites/hud.txt', 'valve/cached.wad',
                     'valve/maps/graphs/c1a0.nod', 'platform/resource/GameMenu.res',
                     'valve_WON/WON.cfg', 'valve_WON/cl_dlls/client.dll'):
            self.assertTrue(keep_game_file(Path(name)), name)
        for name in ('hlds.exe', 'hltv.exe', 'hw.dll.original', 'qconsole.log', 'uhl-frame-1.bmp',
                     'uhl-input-1.tsv', 'RInput.dll', 'valve_WON/cl_dlls/client.stock.dll',
                     'valve_WON/SAVE/quick.sav', 'valve_WON/config.cfg', 'valve/config.cfg',
                     'valve_WON/userconfig.cfg', 'valve_WON/private-route.cfg', 'valve/custom.hpk', 'valve/tempdecal.wad',
                     'valve/dlls/hl_i386.so', 'platform/config/ServerBrowser.vdf',
                     'platform/Friends/servers.vdf', '../private.txt'):
            self.assertFalse(keep_game_file(Path(name)), name)

    def fixture(self, root):
        source = root / 'playtest';source.mkdir()
        entries = {'Half-Life/hl.exe': b'exe', 'Half-Life/hw.dll': b'engine',
            'Half-Life/UHLCompat.dll': b'old adapter', 'Half-Life/hl.exe.manifest': b'dpi',
            'Half-Life/valve_WON/dlls/hl.dll': b'server', 'Half-Life/valve_WON/WON.cfg': b'clockwindow 0',
            'Half-Life/valve_WON/cl_dlls/client.dll': b'adapted client',
            'Half-Life/valve_WON/cl_dlls/client.stock.dll': b'backup',
            'Half-Life/valve_WON/sprites/hud.txt': b'stock active',
            'Half-Life/valve_WON/config.cfg': b'PRIVATE BINDS',
            'Half-Life/valve_WON/SAVE/quick.sav': b'PRIVATE SAVE',
            'Half-Life/valve/config.cfg': b'PRIVATE NAME',
            'Half-Life/platform/config/FriendsData.vdf': b'PRIVATE STATE',
            'Half-Life/uhl-frame-old.bmp': b'generated capture',
            'Half-Life/valve/gfx/shell/logo.bmp': b'real image asset',
            'HUD-presets/ag/hud.txt': b'AG HUD', 'HUD-presets/stock/hud.txt': b'stock HUD',
            'HUD-presets/compact/hud.txt': b'compact HUD',
            'HUD-presets/presets.json': b'PRIVATE DEVELOPER PATH',
            'HUD-presets/crosshairs/original/crosshairs.spr': b'stock crosshairs',
            'HUD-presets/crosshairs/ultimatehl/crosshairs.spr': b'ultimatehl crosshairs',
            'HUD-presets/crosshairs/ultimatehl/notes.txt': b'PRIVATE DRAFT',
            'Half-Life/valve_WON/sprites/crosshairs.spr': b'stock crosshairs',
            'HUD-presets/fonts/original/TrackerScheme.res': b'plain fonts',
            'HUD-presets/fonts/smooth/TrackerScheme.res': b'smooth fonts',
            'Half-Life/platform/resource/TrackerScheme.res': b'plain fonts',
            'BXT/Injector.exe': b'injector', 'BXT/BunnymodXT.dll': b'bxt', 'BXT/LICENSE': b'license',
            'BXT/Tankfird-features.md': b'feature notes', 'adapter-backups/old.dll': b'old',
            'bxt-playtest.log': b'log', 'TESTING.md': b'test evidence'}
        for name, data in entries.items():
            path = source / name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(data)
        native, launcher = root / 'new.dll', root / 'new.exe'
        native.write_bytes(b'new adapter');launcher.write_bytes(b'new launcher')
        return source, native, launcher

    def test_clean_copy_has_ag_default_safe_controls_and_no_source_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);source, native, launcher = self.fixture(root)
            before = {p.relative_to(source): p.read_bytes() for p in source.rglob('*') if p.is_file()}
            target = root / 'clean';target.mkdir()
            report = copy_payload(source, target, native, launcher)
            self.assertEqual((target / 'Half-Life/valve_WON/sprites/hud.txt').read_bytes(), b'AG HUD')
            self.assertEqual((target / 'HUD-presets/stock/hud.txt').read_bytes(), b'stock HUD')
            self.assertEqual((target / 'Half-Life/valve/gfx/shell/logo.bmp').read_bytes(), b'real image asset')
            self.assertIn('sensitivity "3"', (target / 'Half-Life/valve_WON/config.cfg').read_text())
            self.assertIn('uhl_load_fps_min 0', (target / 'Half-Life/valve_WON/uhl_preferences.cfg').read_text())
            self.assertIn('uhl_render_scale 2', (target / 'Half-Life/valve_WON/uhl_preferences.cfg').read_text())
            self.assertEqual((target / 'Half-Life/UHLCompat.dll').read_bytes(), b'new adapter')
            self.assertEqual((target / 'BXT/LICENSE').read_bytes(), b'license')
            self.assertTrue((target / 'BXT/Tankfird-features.txt').is_file())
            self.assertEqual(sorted(p.name for p in target.glob('*.bat')), ['Play - windowed control.bat', 'START HERE.bat'])
            self.assertFalse((target / 'Half-Life/valve_WON/SAVE').exists())
            self.assertFalse((target / 'HUD-presets/presets.json').exists())
            self.assertEqual((target / 'Half-Life/valve_WON/sprites/crosshairs.spr').read_bytes(), b'ultimatehl crosshairs')
            self.assertEqual((target / 'HUD-presets/crosshairs/original/crosshairs.spr').read_bytes(), b'stock crosshairs')
            self.assertFalse((target / 'HUD-presets/crosshairs/ultimatehl/notes.txt').exists())
            self.assertEqual((target / 'Half-Life/platform/resource/TrackerScheme.res').read_bytes(), b'smooth fonts')
            self.assertIn('Half-Life/uhl-frame-old.bmp', report['excluded_files'])
            self.assertFalse(any(b'PRIVATE' in p.read_bytes() for p in target.rglob('*') if p.is_file()))
            self.assertEqual(before, {p.relative_to(source): p.read_bytes() for p in source.rglob('*') if p.is_file()})
            original = dict(client={'patched_sha256': sha(target / 'Half-Life/valve_WON/cl_dlls/client.dll')},
                unchanged_baseline_files={r: sha(target / 'Half-Life' / r) for r in
                    ('hl.exe', 'hw.dll', 'valve_WON/dlls/hl.dll', 'valve_WON/WON.cfg')})
            write_manifest(target, original);verify_distribution(target)
            self.assertEqual(json.loads((target / 'stage-manifest.json').read_text())['hud_preset_initial'], 'ag')
            self.assertEqual(json.loads((target / 'stage-manifest.json').read_text())['crosshair_style_initial'], 'ultimatehl')
            self.assertEqual(json.loads((target / 'stage-manifest.json').read_text())['text_style_initial'], 'smooth')
            self.assertEqual(json.loads((target / 'stage-manifest.json').read_text())['client_layout'], 'patched')
            live = target / 'Half-Life/valve_WON/sprites/crosshairs.spr'
            live.write_bytes(b'stock crosshairs')
            with self.assertRaisesRegex(ValueError, 'crosshair style not installed'):verify_distribution(target)
            live.write_bytes(b'ultimatehl crosshairs')
            (target / 'private.log').write_bytes(b'not tracked')
            with self.assertRaisesRegex(ValueError, 'Untracked'):verify_distribution(target)
            (target / 'private.log').unlink()
            (target / 'Half-Life/valve_WON/SAVE').mkdir()
            (target / 'Half-Life/valve_WON/SAVE/leaked.sav').write_bytes(b'private')
            with self.assertRaisesRegex(ValueError, 'Non-runtime file'):verify_distribution(target)
            (target / 'Half-Life/valve_WON/SAVE/leaked.sav').unlink()
            (target / 'Half-Life/hw.dll').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Protected game file'):verify_distribution(target)

    def test_legit_layout_ships_the_unmodified_client_and_an_attaching_bxt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);source, native, launcher = self.fixture(root)
            bxt = root / 'uhl-bxt.dll';bxt.write_bytes(b'BXT built with UHL_AttachClient')
            stock = root / 'stock.dll';stock.write_bytes(b'stock client')
            target = root / 'clean';target.mkdir()
            copy_payload(source, target, native, launcher, bxt, stock)
            self.assertEqual((target / CLIENT).read_bytes(), b'stock client')
            self.assertEqual((target / 'BXT/BunnymodXT.dll').read_bytes(), bxt.read_bytes())
            original = dict(client={'patched_sha256': sha(source / CLIENT)},
                unchanged_baseline_files={r: sha(target / 'Half-Life' / r) for r in
                    ('hl.exe', 'hw.dll', 'valve_WON/dlls/hl.dll', 'valve_WON/WON.cfg')})
            with patch.object(pd, 'CLIENT_SHA256', sha(stock)):
                write_manifest(target, original, legit=True);verify_distribution(target)
                self.assertEqual(json.loads((target / 'stage-manifest.json').read_text())['client_layout'], 'stock')
                (target / CLIENT).write_bytes(b'adapted client')
                with self.assertRaisesRegex(ValueError, 'unmodified client'):verify_distribution(target)
                with self.assertRaisesRegex(ValueError, 'cannot attach'):
                    pd.validate_source(source, native, launcher, native, stock)
                with self.assertRaisesRegex(ValueError, 'both'):
                    pd.validate_source(source, native, launcher, bxt, None)

    def test_no_existing_or_nested_output_is_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);source = root / 'source';source.mkdir()
            with self.assertRaisesRegex(ValueError, 'NEW output'):
                package(source, source, root / 'dll', root / 'exe')
            with self.assertRaisesRegex(ValueError, 'non-nested'):
                package(source, source / 'inside', root / 'dll', root / 'exe')
            archive = root / 'existing.zip';archive.write_bytes(b'existing')
            with self.assertRaisesRegex(ValueError, 'NEW output'):
                package(source, root / 'new', root / 'dll', root / 'exe', archive)
            self.assertEqual(archive.read_bytes(), b'existing')


if __name__ == '__main__':
    unittest.main()
