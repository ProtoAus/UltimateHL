"""PE structure/regression tests; no game process is started."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

import pefile

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
import patch_client as patch

SOURCE = PROJECT.parent / "Half-Life 2005 WON/Half-Life/valve_WON/cl_dlls/client.dll"


class PatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = SOURCE.read_bytes()
        cls.output = patch.build_client(cls.original, patch.MOUSE_ACTIVE_RVA)
        cls.before = pefile.PE(data=cls.original)
        cls.after = pefile.PE(data=cls.output)

    def test_exact_baseline(self):
        self.assertEqual(hashlib.sha256(self.original).hexdigest(), patch.CLIENT_SHA256)

    def test_original_text_and_data_untouched(self):
        for section in self.before.sections:
            if section.Name.rstrip(b"\0") in (b".text", b".data", b".reloc"):
                start, size = section.PointerToRawData, section.SizeOfRawData
                self.assertEqual(self.original[start:start + size], self.output[start:start + size])

    def test_existing_imports_and_iat_unchanged(self):
        original_iat = self.before.OPTIONAL_HEADER.DATA_DIRECTORY[12]
        self.assertEqual(self.before.get_data(original_iat.VirtualAddress, original_iat.Size),
                         self.after.get_data(original_iat.VirtualAddress, original_iat.Size))
        for before, after in zip(self.before.DIRECTORY_ENTRY_IMPORT, self.after.DIRECTORY_ENTRY_IMPORT):
            self.assertEqual(before.dll, after.dll)
            self.assertEqual(before.struct.__pack__(), after.struct.__pack__())
        added = self.after.DIRECTORY_ENTRY_IMPORT[-1]
        self.assertEqual(added.dll, b"UHLCompat.dll")
        self.assertEqual([i.name.decode() for i in added.imports], list(patch.ADAPTERS.values()))

    def test_all_other_exports_unchanged(self):
        before = {e.name: e.address for e in self.before.DIRECTORY_ENTRY_EXPORT.symbols}
        after = {e.name: e.address for e in self.after.DIRECTORY_ENTRY_EXPORT.symbols}
        self.assertEqual(before.keys(), after.keys())
        for name, address in before.items():
            if name.decode() not in patch.ADAPTERS:
                self.assertEqual(address, after[name])

    def test_adapter_instructions_and_relocations(self):
        after = {e.name.decode(): e.address for e in self.after.DIRECTORY_ENTRY_EXPORT.symbols}
        iat = self.after.DIRECTORY_ENTRY_IMPORT[-1].struct.FirstThunk
        relocations = {entry.rva for block in self.after.DIRECTORY_ENTRY_BASERELOC
                       for entry in block.entries if entry.type == 3}
        for index, name in enumerate(patch.ADAPTERS):
            code = self.after.get_data(after[name], 6)
            self.assertEqual(code[:2], b"\xff\x25")
            self.assertEqual(struct.unpack("<I", code[2:])[0],
                             self.after.OPTIONAL_HEADER.ImageBase + iat + index * 4)
            self.assertIn(after[name] + 2, relocations)
        for block in self.before.DIRECTORY_ENTRY_BASERELOC:
            for entry in block.entries:
                if entry.type == 3:
                    self.assertIn(entry.rva, relocations)

    def test_profile(self):
        section = next(s for s in self.after.sections if s.Name.rstrip(b"\0") == b".uhl")
        values = struct.unpack(patch.PROFILE_FORMAT,
                               self.after.get_data(section.VirtualAddress, patch.PROFILE_SIZE))
        self.assertEqual(values[:3], (b"UHLRAW3\0", 3, 64))
        before = {e.name.decode(): e.address for e in self.before.DIRECTORY_ENTRY_EXPORT.symbols}
        self.assertEqual(list(values[3:12]), [before[name] for name in patch.ADAPTERS])
        self.assertEqual(values[12], patch.MOUSE_ACTIVE_RVA)
        for section in self.after.sections:
            if section.Name.startswith(b".uhl"):
                self.assertNotEqual(section.Characteristics & 0xA0000000, 0xA0000000,
                                    "new sections must not be RWX")
        self.assertEqual(self.after.OPTIONAL_HEADER.CheckSum, self.after.generate_checksum())

    def test_refuse_unknown_and_in_place(self):
        with tempfile.TemporaryDirectory() as temp:
            unknown = Path(temp) / "unknown.dll"
            unknown.write_bytes(b"not the client")
            with self.assertRaisesRegex(ValueError, "Unsupported client"):
                patch.patch_file(unknown, Path(temp) / "output.dll")
            with self.assertRaisesRegex(ValueError, "NEW file"):
                patch.patch_file(SOURCE, SOURCE)

    def test_refuse_double_patch(self):
        with self.assertRaisesRegex(ValueError, "already patched"):
            patch.build_client(self.output, patch.MOUSE_ACTIVE_RVA)


if __name__ == "__main__":
    unittest.main()
