import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from make_hud_presets import compact_text,make as make_hud,validate_ag
from make_obs_test_collection import make as make_obs
from make_game_menu import install as install_menu

HUD = """4
number_0 320 320hud2 0 0 12 16
number_0 640 640hud7 0 0 20 24
crosshair 320 320hud1 8 8 10 10
crosshair 640 640hud1 16 16 20 20
"""


class VisualPresetTests(unittest.TestCase):
    def test_native_art_not_resampling_and_reticle_preserved(self):
        compact,count=compact_text(HUD)
        self.assertEqual(count,1)
        self.assertIn("number_0\t640\t320hud2\t0\t0\t12\t16",compact)
        self.assertIn("crosshair 640 640hud1 16 16 20 20",compact)
        self.assertEqual(compact.splitlines()[0],"4")
        zoom="2\nzoom 320 low 0 0 10 10\nzoom 640 high 0 0 20 20\n"
        self.assertEqual(compact_text(zoom),(zoom,0))

    def test_hud_presets_keep_source_and_allow_stock_rollback(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=root/"Half-Life";source=game/"valve/sprites/hud.txt"
            source.parent.mkdir(parents=True);source.write_text(HUD)
            result=make_hud(game,root/"HUD-presets")
            self.assertEqual(result,{"hud.txt":1});self.assertEqual(source.read_text(),HUD)
            self.assertEqual((root/"HUD-presets/stock/hud.txt").read_text(),HUD)
            self.assertEqual((game/"valve_WON/sprites/hud.txt").read_text(),compact_text(HUD)[0])
            with self.assertRaises(ValueError):make_hud(game,root/"HUD-presets")

    def test_stock_preset_is_byte_identical_including_crlf(self):
        with tempfile.TemporaryDirectory()as folder:
            root=Path(folder);game=root/'Half-Life';source=game/'valve/sprites/hud.txt'
            source.parent.mkdir(parents=True);raw=HUD.replace('\n','\r\n').encode('latin-1');source.write_bytes(raw)
            make_hud(game,root/'HUD-presets','stock')
            self.assertEqual((root/'HUD-presets/stock/hud.txt').read_bytes(),raw)
            self.assertEqual((game/'valve_WON/sprites/hud.txt').read_bytes(),raw)

    def test_ag_assets_are_separate_and_stock_remains_default(self):
        with tempfile.TemporaryDirectory()as folder:
            root=Path(folder);game=root/'Half-Life';baseline=game/'valve/sprites/hud.txt'
            baseline.parent.mkdir(parents=True);baseline.write_text(HUD)
            ag=root/'AG';ag.mkdir();listing='2\nnumber_0 320 ag_digits 0 0 30 40\nnumber_0 640 ag_digits 0 0 30 40\n'
            (ag/'hud.txt').write_text(listing)
            sprite=struct.pack('<4s3if3ifi',b'IDSP',2,0,2,32.,64,64,1,0.,0)+struct.pack('<H',256)+bytes(768)+struct.pack('<5i',0,0,0,64,64)+bytes(4096)
            (ag/'ag_digits.spr').write_bytes(sprite)
            make_hud(game,root/'presets','stock',ag)
            self.assertEqual((game/'valve_WON/sprites/hud.txt').read_bytes(),baseline.read_bytes())
            self.assertEqual((game/'valve_WON/sprites/ag_digits.spr').read_bytes(),sprite)
            self.assertEqual((root/'presets/ag/hud.txt').read_text(),listing)
            self.assertTrue((root/'presets/compact/hud.txt').is_file())
            self.assertEqual(list(root.glob('*.bat')),[])
            (ag/'hud.txt').write_text(listing.replace('30 40','65 40'))
            with self.assertRaisesRegex(ValueError,'crop outside'):validate_ag(game,ag)
            (ag/'hud.txt').write_text(listing.replace('ag_digits','../escape'))
            with self.assertRaisesRegex(ValueError,'Unsafe'):validate_ag(game,ag)

    def test_menu_link_and_non_resetting_preferences(self):
        with tempfile.TemporaryDirectory()as folder:
            game=Path(folder);source=game/'valve/resource/GameMenu.res';source.parent.mkdir(parents=True)
            text='"GameMenu"\n{\n"14" {"label" "Options" "command" "OpenOptionsDialog"}\n}\n'
            source.write_text(text);target=install_menu(game)
            self.assertEqual(source.read_text(),text)
            self.assertIn('"command" "engine uhl_settings"',target.read_text())
            self.assertIn('OpenOptionsDialog',target.read_text())
        defaults=(Path(__file__).resolve().parents[1]/'resources/uhl_defaults.cfg').read_text()
        commands=[line for line in defaults.splitlines()if line.strip()and not line.lstrip().startswith('//')]
        self.assertFalse(any(line.startswith('uhl_')for line in commands))
        self.assertFalse(any(';'in line for line in defaults.splitlines()))

    def test_full_resolution_game_collection_is_opt_in_and_has_no_other_hooks(self):
        with tempfile.TemporaryDirectory()as folder:
            source=Path(folder)/'old.json';out=Path(folder)/'new.json'
            source.write_text(json.dumps({'sources':[{'id':'monitor_capture','settings':{}},{'id':'scene'}],
                'resolution':{'x':1920,'y':1200}}));before=source.read_bytes();make_obs(source,out,'game')
            result=json.loads(out.read_text());self.assertEqual(source.read_bytes(),before)
            self.assertEqual(result['resolution'],{'x':1920,'y':1200})
            self.assertEqual([s['id']for s in result['sources']],['game_capture','scene'])
            self.assertEqual(result['sources'][0]['settings']['window'],'Half-Life:Valve001:hl.exe')
            self.assertEqual(result['sources'][0]['filters'],[])

    def test_wgc_collection_is_display_only_and_does_not_edit_obs(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/"saved-obs.json";destination=Path(folder)/"new-test.json"
            data={"name":"Original","sources":[
                {"name":"Desktop","id":"monitor_capture","uuid":"old-display","settings":{"monitor_id":"test-monitor","method":1},"filters":[{"id":"color_key_filter"}]},
                {"name":"Scene","id":"scene","uuid":"old-scene","settings":{"items":[]}},
                {"name":"Other game hook","id":"game_capture"}],"resolution":{"x":1920,"y":1200},"version":2}
            source.write_text(json.dumps(data));before=source.read_bytes();make_obs(source,destination)
            self.assertEqual(source.read_bytes(),before)
            result=json.loads(destination.read_text());capture,scene=result["sources"]
            self.assertEqual(capture["settings"],{"monitor_id":"test-monitor","method":2,"cursor":True})
            self.assertEqual(capture["filters"],[])
            self.assertEqual(scene["settings"]["items"][0]["source_uuid"],capture["uuid"])
            self.assertIsNone(result["AuxAudioDevice1"])
            with self.assertRaises(ValueError):make_obs(source,destination)


if __name__=="__main__":unittest.main()
