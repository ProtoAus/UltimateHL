"""Installer transaction tests using fake binary bytes; no game/OS settings calls."""
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import update_native as updater
from patch_client import ADAPTERS, sha256


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.destination = self.root / "playtest"
        self.game = self.destination / "Half-Life"
        self.build = self.root / "build"
        for path, data in ((self.game / "hl.exe", b"engine"),
                           (self.game / "valve_WON/cl_dlls/client.dll", b"client"),
                           (self.game / "UHLCompat.dll", b"old adapter"),
                           (self.game / "hl.exe.manifest", b"DPI manifest"),
                           (self.destination / "UltimateHL.exe", b"old launcher"),
                           (self.build / "UHLCompat.dll", b"new adapter"),
                           (self.build / "UltimateHL.exe", b"new launcher"),
                           (self.game / "valve_WON/config.cfg", b"user binds"),
                           (self.game / "valve_WON/SAVE/user.sav", b"user save")):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.manifest = self.destination / "stage-manifest.json"
        self.manifest.write_text(json.dumps({"adapter_version": 3,
            "unchanged_baseline_files": {"hl.exe": sha256(b"engine")},
            "client": {"patched_sha256": sha256(b"client")},
            "native_sha256": sha256(b"old adapter"), "launcher_sha256": sha256(b"old launcher"),
            "dpi_manifest_sha256": sha256(b"DPI manifest")}))
        self.before = self.manifest.read_bytes()
        fake = SimpleNamespace(FILE_HEADER=SimpleNamespace(Machine=0x14c),
            DIRECTORY_ENTRY_EXPORT=SimpleNamespace(symbols=[SimpleNamespace(name=name.encode()) for name in ADAPTERS.values()]))
        self.parse = patch.object(updater.pefile, "PE", return_value=fake)
        self.parse.start()
        self.addCleanup(self.parse.stop)

    def update(self):
        updater.update(self.destination, self.build / "UHLCompat.dll")

    def untouched(self):
        self.assertEqual((self.game / "UHLCompat.dll").read_bytes(), b"old adapter")
        self.assertEqual((self.destination / "UltimateHL.exe").read_bytes(), b"old launcher")
        self.assertEqual(self.manifest.read_bytes(), self.before)

    def test_updates_both_and_keeps_user_data(self):
        self.update()
        self.assertEqual((self.game / "UHLCompat.dll").read_bytes(), b"new adapter")
        self.assertEqual((self.destination / "UltimateHL.exe").read_bytes(), b"new launcher")
        self.assertEqual((self.game / "valve_WON/config.cfg").read_bytes(), b"user binds")
        self.assertEqual((self.game / "valve_WON/SAVE/user.sav").read_bytes(), b"user save")
        self.assertEqual(len(list((self.destination / "adapter-backups").iterdir())), 2)
        metadata = self.manifest.read_bytes()
        self.update()
        self.assertEqual(self.manifest.read_bytes(), metadata)

    def test_refuses_changed_client(self):
        (self.game / "valve_WON/cl_dlls/client.dll").write_bytes(b"unknown client")
        with self.assertRaises(ValueError): self.update()
        self.untouched()

    def test_accepts_the_unmodified_client_of_the_legit_layout(self):
        (self.game / "valve_WON/cl_dlls/client.dll").write_bytes(b"stock client")
        with patch.object(updater, "CLIENT_SHA256", sha256(b"stock client")): self.update()
        self.assertEqual((self.game / "UHLCompat.dll").read_bytes(), b"new adapter")

    def test_refuses_changed_launcher(self):
        (self.destination / "UltimateHL.exe").write_bytes(b"unknown launcher")
        with self.assertRaises(ValueError): self.update()
        self.assertEqual((self.game / "UHLCompat.dll").read_bytes(), b"old adapter")
        self.assertEqual(self.manifest.read_bytes(), self.before)

    def test_refuses_v2(self):
        metadata = json.loads(self.before)
        metadata["adapter_version"] = 2
        self.manifest.write_text(json.dumps(metadata))
        with self.assertRaises(ValueError): self.update()
        self.assertEqual((self.game / "UHLCompat.dll").read_bytes(), b"old adapter")

    def fail_replacement(self, target):
        replace = os.replace
        failed = False
        def fail_once(source, destination):
            nonlocal failed
            if Path(destination) == target and not failed:
                failed = True
                raise PermissionError("simulated locked file")
            return replace(source, destination)
        with patch.object(updater.os, "replace", side_effect=fail_once):
            with self.assertRaises(PermissionError): self.update()
        self.untouched()
        self.assertFalse(list(self.game.glob("tmp*")))
        self.assertFalse(list(self.destination.glob("tmp*")))

    def test_rolls_back_if_launcher_replacement_fails(self):
        self.fail_replacement(self.destination / "UltimateHL.exe")

    def test_rolls_back_if_manifest_replacement_fails(self):
        self.fail_replacement(self.manifest)


if __name__ == "__main__":
    unittest.main()
