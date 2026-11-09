import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import install_addon
import package_addon


class PublicAddonTests(unittest.TestCase):
    def test_public_archive_is_explicit_addon_allowlist(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binaries = root / 'bin'
            binaries.mkdir()
            for name in ['UHLCompat.dll', 'UltimateHL.exe']:
                (binaries / name).write_bytes(b'fake Win32 fixture')
            bxt = root / 'BunnymodXT.dll'
            bxt.write_bytes(b'fake UHL_AttachClient\0')
            # Even if these exist beside the build, the packager must not read them.
            for name in ['hl.exe', 'client.dll', 'hw.dll', 'personal.sav', 'hud.spr', 'Injector.exe']:
                (binaries / name).write_bytes(b'NEVER PUBLISH')
            archive = root / 'addon.zip'
            with patch.object(package_addon.pefile, 'PE', return_value=SimpleNamespace(FILE_HEADER=SimpleNamespace(Machine=0x14c))):
                package_addon.package(archive, binaries, bxt)
                with self.assertRaises(FileExistsError):
                    package_addon.package(archive, binaries, bxt)
            with zipfile.ZipFile(archive) as zipped:
                names = {name.split('/', 1)[1] for name in zipped.namelist()}
                binary_names = {name for name in names if name.endswith(('.exe', '.dll'))}
                self.assertEqual(binary_names, {'bin/UHLCompat.dll', 'bin/UltimateHL.exe', 'bin/BunnymodXT.dll'})
                self.assertFalse(any('Half-Life/' in name or name.endswith(('.spr', '.sav', '.pak')) for name in names))
                prefix = f'UltimateHL-v{package_addon.VERSION}-addon/'
                for line in zipped.read(prefix + 'SHA256SUMS.txt').decode().splitlines():
                    digest, name = line.split('  ', 1)
                    self.assertEqual(digest, hashlib.sha256(zipped.read(prefix + name)).hexdigest())

    def test_clean_install_is_separate_and_does_not_copy_personal_state(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            game, binaries, ag = root / 'game', root / 'bin', root / 'ag'
            game.mkdir(); binaries.mkdir(); ag.mkdir()
            fixtures = {'hl.exe': b'owned executable', 'hw.dll': b'owned engine',
                        'valve_WON/cl_dlls/client.dll': b'owned stock client',
                        'valve_WON/dlls/hl.dll': b'owned server', 'valve_WON/WON.cfg': b'owned config',
                        'valve_WON/config.cfg': b'PERSONAL BINDS', 'valve_WON/SAVE/quick.sav': b'PERSONAL SAVE',
                        'qconsole.log': b'PRIVATE LOG', 'platform/config/user.vdf': b'PRIVATE STATE'}
            for name, data in fixtures.items():
                path = game / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            for name in ['UHLCompat.dll', 'UltimateHL.exe', 'BunnymodXT.dll', 'Injector.exe']:
                (binaries / name).write_bytes(b'addon fixture')
            destination = root / 'new'

            def fake_presets(out_game, presets, selected, source):
                self.assertEqual(selected, 'ag')
                presets.mkdir(); (presets / 'presets.json').write_text('{}')

            with patch.object(install_addon, 'validate_inputs'), patch.object(install_addon, 'make_hud_presets', side_effect=fake_presets), \
                    patch.object(install_addon, 'install_menu'), patch.object(install_addon, 'install_fonts'):
                install_addon.install(game, destination, binaries, binaries / 'BunnymodXT.dll', binaries / 'Injector.exe', ag)
                with self.assertRaisesRegex(ValueError, 'already exists'):
                    install_addon.install(game, destination, binaries, binaries / 'BunnymodXT.dll', binaries / 'Injector.exe', ag)
                with self.assertRaisesRegex(ValueError, 'non-nested'):
                    install_addon.install(game, game / 'nested', binaries, binaries / 'BunnymodXT.dll', binaries / 'Injector.exe', ag)
            for name, data in fixtures.items():
                self.assertEqual((game / name).read_bytes(), data, name)
            self.assertFalse((destination / 'Half-Life/valve_WON/SAVE/quick.sav').exists())
            self.assertFalse((destination / 'Half-Life/qconsole.log').exists())
            self.assertFalse((destination / 'Half-Life/platform/config/user.vdf').exists())
            self.assertNotIn(b'PERSONAL BINDS', (destination / 'Half-Life/valve_WON/config.cfg').read_bytes())
            self.assertEqual((destination / 'Half-Life/valve_WON/cl_dlls/client.dll').read_bytes(), b'owned stock client')
            self.assertEqual(json.loads((destination / 'stage-manifest.json').read_text())['client_layout'], 'stock')

    def test_unsupported_game_rejected_before_creating_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            game = root / 'game'; game.mkdir(); (game / 'hw.dll').write_bytes(b'unsupported')
            with self.assertRaisesRegex(ValueError, 'Unsupported hw.dll'):
                install_addon.install(game, root / 'new', root / 'bin', root / 'bxt', root / 'injector', root / 'ag')
            self.assertFalse((root / 'new').exists())


if __name__ == '__main__':
    unittest.main()
