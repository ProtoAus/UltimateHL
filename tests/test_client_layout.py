import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from client_layout import CLIENT,switch
from patch_client import sha256

STOCK=sha256(b"stock client")


def make_copy(root,bxt=b"BXT with UHL_AttachClient"):
    copy=root/"copy";(copy/"BXT").mkdir(parents=True);(copy/CLIENT).parent.mkdir(parents=True)
    (copy/"stage-manifest.json").write_text(json.dumps({"client":{"patched_sha256":sha256(b"patched client")}}))
    (copy/CLIENT).write_bytes(b"patched client");(copy/"BXT/BunnymodXT.dll").write_bytes(bxt)
    (copy/"Half-Life/valve_WON/SAVE").mkdir(parents=True);(copy/"Half-Life/valve_WON/SAVE/run.sav").write_bytes(b"save")
    (root/"stock.dll").write_bytes(b"stock client")
    return copy


class ClientLayoutTests(unittest.TestCase):
    def test_round_trip_keeps_both_clients_and_saves(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);copy=make_copy(root)
            self.assertTrue(switch(copy,"stock",root/"stock.dll",STOCK)["changed"])
            self.assertEqual((copy/CLIENT).read_bytes(),b"stock client")
            self.assertEqual(switch(copy,"stock",None,STOCK),{"layout":"stock","changed":False})
            self.assertTrue(switch(copy,"patched",None,STOCK)["changed"])
            self.assertEqual((copy/CLIENT).read_bytes(),b"patched client")
            self.assertTrue(switch(copy,"stock",None,STOCK)["changed"]) # from the kept backup
            self.assertEqual((copy/CLIENT).read_bytes(),b"stock client")
            self.assertEqual((copy/"Half-Life/valve_WON/SAVE/run.sav").read_bytes(),b"save")
            self.assertEqual(sorted(p.name for p in (copy/"adapter-backups").iterdir()),
                             sorted([f"client-{sha256(b'patched client')}.dll",f"client-{STOCK}.dll"]))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);copy=make_copy(root,bxt=b"official BXT")
            with self.assertRaisesRegex(ValueError,"cannot attach"):switch(copy,"stock",root/"stock.dll",STOCK)
            (copy/"BXT/BunnymodXT.dll").write_bytes(b"UHL_AttachClient")
            (root/"stock.dll").write_bytes(b"some other client")
            with self.assertRaisesRegex(ValueError,"wrong SHA-256"):switch(copy,"stock",root/"stock.dll",STOCK)
            (copy/CLIENT).write_bytes(b"unknown client")
            with self.assertRaisesRegex(ValueError,"Unknown client"):switch(copy,"stock",root/"stock.dll",STOCK)
            self.assertEqual((copy/CLIENT).read_bytes(),b"unknown client")


if __name__=="__main__":unittest.main()
