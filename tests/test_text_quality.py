from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from text_quality import SCHEME,install,smooth,style_path

SCHEME_TEXT=('Scheme\r\n{\r\n\tFonts\r\n\t{\r\n\t\t"EngineFont"\r\n\t\t{\r\n'
    '\t\t\t"1"\r\n\t\t\t{\r\n\t\t\t\t"name"\t\t"Verdana"\r\n\t\t\t\t"tall"\t\t"12"\r\n\t\t\t\t"dropshadow"\t"1"\r\n\t\t\t}\r\n'
    '\t\t\t"2"\r\n\t\t\t{\r\n\t\t\t\t"name"\t\t"Verdana"\r\n\t\t\t\t"tall"\t\t"24"\r\n\t\t\t\t"antialias"\t"1"\r\n\t\t\t}\r\n\t\t}\r\n'
    '\t\t"CreditsFont"\r\n\t\t{\r\n\t\t\t"1"\r\n\t\t\t{\r\n\t\t\t\t"name"\t\t"Trebuchet MS"\r\n\t\t\t}\r\n\t\t}\r\n\t}\r\n}\r\n')


class TextQualityTests(unittest.TestCase):
    def test_only_engine_font_sizes_gain_antialias(self):
        out=smooth(SCHEME_TEXT)
        self.assertIn('"dropshadow"\t"1"\r\n\t\t\t\t"antialias"\t"1"\r\n\t\t\t}',out)
        self.assertEqual(out.count('"antialias"'),2) # size 2 already had it; CreditsFont untouched
        self.assertIn('"Trebuchet MS"\r\n\t\t\t}',out)
        self.assertEqual(out.replace('\t\t\t\t"antialias"\t"1"\r\n','',1),SCHEME_TEXT)
        self.assertEqual(smooth(out),out)

    def test_install_and_revert(self):
        with tempfile.TemporaryDirectory() as folder:
            game=Path(folder)/"Half-Life";live=game/SCHEME;live.parent.mkdir(parents=True)
            raw=SCHEME_TEXT.encode("latin-1");live.write_bytes(raw)
            install(game);install(game)
            self.assertEqual(live.read_bytes(),smooth(SCHEME_TEXT).encode("latin-1"))
            self.assertEqual(style_path(game,"original").read_bytes(),raw)
            install(game,revert=True)
            self.assertEqual(live.read_bytes(),raw);self.assertFalse((Path(folder)/"HUD-presets/fonts").exists())


if __name__=="__main__":unittest.main()
