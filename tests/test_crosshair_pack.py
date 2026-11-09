import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from crosshair_pack import Sprite,export,fill_crosshair,install,pack,pixels,style_path

W=72  # three 24x24 cells: slot 1 (2x2 square), the glock plus, an empty cell


def sheet_sprite(width=W,height=24):
    palette=bytearray(768);palette[3:6]=bytes((255,154,0));palette[6:9]=bytes((255,0,0))
    indices=bytearray([255])*(width*height)
    for x,y in ((11,11),(12,11),(11,12),(12,12)):indices[y*width+x]=1
    if width>=48:
        for i in range(4,20):indices[12*width+24+i]=1;indices[i*width+36]=1
        indices[12*width+36]=2
    header=struct.pack('<4s3if3ifi',b'IDSP',2,2,3,34.,width,height,1,0.,1)
    return header+struct.pack('<H',256)+bytes(palette)+struct.pack('<5i',0,-width//2,height//2,width,height)+bytes(indices)


def listing(*entries,newline='\n'):
    return (str(len(entries))+newline+''.join(e+newline for e in entries)).encode()


GLOCK=listing('weapon\t\t\t640 640hud1\t0\t45\t170\t45','crosshair\t\t320 crosshairs\t24\t0\t24\t24',
              'crosshair\t\t640 crosshairs\t24\t0\t24\t24','autoaim\t\t\t640 crosshairs\t24\t0\t24\t24')
CROWBAR=listing('weapon\t\t\t320 320hud1\t0\t0\t80\t20','weapon\t\t\t640 640hud1\t0\t0\t170\t45',newline='\r\n')
GRENADE=listing('weapon\t\t\t640 640hud1\t0\t0\t170\t45','crosshair\t\t640 crosshairs\t24\t0\t24\t24')
SNARK=listing('weapon\t\t\t640 640hud1\t0\t0\t170\t45','crosshair\t\t640 crosshairs\t48\t0\t24\t24')
GAUSS=listing('weapon\t\t\t640 640hud1\t0\t0\t170\t45','crosshair\t\t640 crosshairs\t24\t0\t24\t24')
SLOT1=b'crosshair\t\t%s crosshairs\t0\t0\t24\t24'
GLOCK_FILL=b'crosshair\t\t%s crosshairs\t24\t0\t24\t24'


def make_game(root):
    game=root/'Half-Life';(game/'valve/sprites').mkdir(parents=True);(game/'valve_WON/sprites').mkdir(parents=True)
    (game/'valve/sprites/crosshairs.spr').write_bytes(sheet_sprite())
    for folder in (game/'valve_WON/sprites',root/'HUD-presets/stock'):
        folder.mkdir(parents=True,exist_ok=True)
        for name,data in {'weapon_9mmhandgun.txt':GLOCK,'weapon_crowbar.txt':CROWBAR,
                          'weapon_handgrenade.txt':GRENADE,'weapon_snark.txt':SNARK,'weapon_gauss.txt':GAUSS,
                          'weapon_question.txt':CROWBAR}.items():(folder/name).write_bytes(data)
    return game


class CrosshairPackTests(unittest.TestCase):
    def test_unedited_round_trip_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root)
            self.assertEqual(export(game,root/'out')['cells'],2)  # glock/grenade/gauss share a cell
            manifest=json.loads((root/'out/manifest.json').read_text())
            self.assertEqual([c['file'] for c in manifest['cells']],
                             ['cells/9mmhandgun_crosshair.png','cells/snark_crosshair.png'])
            self.assertEqual(sorted(manifest['cells'][0]['used_by'])[0],'weapon_9mmhandgun autoaim 640')
            image=Image.open(root/'out/crosshairs.png')
            self.assertEqual(image.getpixel((60,12))[3],0);self.assertEqual(image.getpixel((36,4)),(255,154,0,255))
            result=pack(root/'out',root/'new.spr')
            self.assertTrue(result['unchanged']);self.assertEqual((root/'new.spr').read_bytes(),sheet_sprite())
            with self.assertRaises(ValueError):pack(root/'out',root/'new.spr')
            with self.assertRaises(ValueError):export(game,root/'out')

    def test_edited_cell_changes_only_that_cell_and_keeps_indices(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root);export(game,root/'out')
            cell=Image.open(root/'out/cells/9mmhandgun_crosshair.png').convert('RGBA')
            cell.putpixel((0,0),(0,255,0,255));cell.putpixel((12,4),(0,0,0,10));cell.save(root/'out/cells/9mmhandgun_crosshair.png')
            self.assertEqual(pack(root/'out',root/'new.spr')['edited_cells'],['cells/9mmhandgun_crosshair.png'])
            old,new=Sprite(sheet_sprite()),Sprite((root/'new.spr').read_bytes())
            self.assertEqual(new.palette[new.indices[24]],(0,255,0));self.assertEqual(new.indices[4*W+36],255)
            self.assertEqual(new.palette[1],(255,154,0));self.assertEqual(new.palette[2],(255,0,0))
            changed=[i for i,(a,b) in enumerate(zip(old.indices,new.indices)) if a!=b]
            self.assertEqual(changed,[24,4*W+36]);self.assertEqual(new.header,old.header)

    def test_sheet_edits_survive_unchanged_cells_and_size_is_enforced(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root);export(game,root/'out')
            sheet=Image.open(root/'out/crosshairs.png').convert('RGBA')
            sheet.putpixel((66,3),(10,20,30,255));sheet.save(root/'out/crosshairs.png')
            pack(root/'out',root/'a.spr');new=Sprite((root/'a.spr').read_bytes())
            self.assertEqual(new.palette[new.indices[3*W+66]],(10,20,30))
            sheet.resize((W*2,48)).save(root/'out/crosshairs.png')
            with self.assertRaisesRegex(ValueError,f'must stay {W}x24'):pack(root/'out',root/'b.spr')

    def test_many_new_colours_requantize_but_keep_transparency(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root);export(game,root/'out')
            sheet=Image.new('RGBA',(W,24))
            sheet.putdata([(x*5,y*10,(x*y)%256,255) if x<24 else (0,0,0,0) for y in range(24) for x in range(W)])
            sheet.save(root/'out/crosshairs.png');pack(root/'out',root/'new.spr')
            new=Sprite((root/'new.spr').read_bytes())
            self.assertTrue(all(new.indices[y*W+x]==255 for y in range(24) for x in range(24,W)))
            self.assertTrue(all(new.indices[y*W+x]<255 for y in range(24) for x in range(24)))
            self.assertTrue(all(p[3]==255 for p in pixels(new.image().crop((0,0,24,24)))))

    def test_install_makes_the_pack_default_and_keeps_original_selectable(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root);pack_file=root/'p.spr';pack_file.write_bytes(sheet_sprite(24,24))
            target=game/'valve_WON/sprites/crosshairs.spr'
            install(pack_file,game)
            self.assertEqual(target.read_bytes(),sheet_sprite(24,24))
            self.assertEqual(style_path(game,'ultimatehl').read_bytes(),sheet_sprite(24,24))
            self.assertEqual(style_path(game,'original').read_bytes(),sheet_sprite())
            self.assertFalse((root/'crosshair-originals').exists())  # nothing hand-installed to keep
            install(None,game,revert=True)
            self.assertFalse(target.exists());self.assertFalse((root/'HUD-presets/crosshairs').exists())
            self.assertTrue((root/'HUD-presets/stock').is_dir())
            (root/'bad.spr').write_bytes(b'nope')
            with self.assertRaises(ValueError):install(root/'bad.spr',game)

    def test_install_keeps_a_hand_installed_sheet_for_revert(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root);pack_file=root/'p.spr';pack_file.write_bytes(sheet_sprite(24,24))
            target=game/'valve_WON/sprites/crosshairs.spr';custom=sheet_sprite(48,24);target.write_bytes(custom)
            install(pack_file,game);install(pack_file,game)
            self.assertEqual(target.read_bytes(),sheet_sprite(24,24))
            install(None,game,revert=True);self.assertEqual(target.read_bytes(),custom)

    def test_fill_uses_slot_1_in_live_and_presets_and_can_switch_and_revert(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root)
            result=fill_crosshair(game)
            filled=['weapon_crowbar.txt','weapon_handgrenade.txt','weapon_snark.txt']
            self.assertEqual(result['changed'],[f'{label}/{name}' for label in ('live','preset-stock') for name in filled])
            crowbar=(game/'valve_WON/sprites/weapon_crowbar.txt').read_bytes()
            self.assertEqual(crowbar,CROWBAR.replace(b'2\r\n',b'4\r\n',1)+SLOT1%b'320'+b'\r\n'+SLOT1%b'640'+b'\r\n')
            snark=(root/'HUD-presets/stock/weapon_snark.txt').read_bytes()
            self.assertTrue(snark.startswith(b'3\n'));self.assertNotIn(b'\t48\t0\t',snark)
            self.assertIn(SLOT1%b'640'+b'\n',snark);self.assertIn(SLOT1%b'320'+b'\n',snark)
            grenade=(game/'valve_WON/sprites/weapon_handgrenade.txt').read_bytes()  # gauss circle replaced
            self.assertEqual(grenade,b'3\nweapon\t\t\t640 640hud1\t0\t0\t170\t45\n'+SLOT1%b'640'+b'\n'+SLOT1%b'320'+b'\n')
            self.assertEqual((game/'valve_WON/sprites/weapon_gauss.txt').read_bytes(),GAUSS)
            self.assertEqual((game/'valve_WON/sprites/weapon_question.txt').read_bytes(),CROWBAR)
            self.assertEqual(fill_crosshair(game),{'changed':[],'restored':[]})
            # Switching to the glock fill starts again from the saved originals.
            self.assertEqual(len(fill_crosshair(game,glock=True)['changed']),6)
            crowbar=(game/'valve_WON/sprites/weapon_crowbar.txt').read_bytes()
            self.assertEqual(crowbar,CROWBAR.replace(b'2\r\n',b'4\r\n',1)+GLOCK_FILL%b'320'+b'\r\n'+GLOCK_FILL%b'640'+b'\r\n')
            self.assertEqual(len(fill_crosshair(game,revert=True)['restored']),6)
            for folder in (game/'valve_WON/sprites',root/'HUD-presets/stock'):
                self.assertEqual((folder/'weapon_crowbar.txt').read_bytes(),CROWBAR)
                self.assertEqual((folder/'weapon_snark.txt').read_bytes(),SNARK)
                self.assertEqual((folder/'weapon_handgrenade.txt').read_bytes(),GRENADE)
            self.assertFalse(any((root/'crosshair-originals').rglob('*.txt')))

    def test_fill_ignores_the_active_style_when_deciding_what_has_no_crosshair(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root)
            (game/'valve_WON/sprites/crosshairs.spr').write_bytes(sheet_sprite(24,24))  # glock cell missing
            self.assertNotIn('live/weapon_gauss.txt',fill_crosshair(game)['changed'])

    def test_fill_refuses_a_miscounted_list(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);game=make_game(root)
            (game/'valve_WON/sprites/weapon_crowbar.txt').write_bytes(b'5\nweapon 640 640hud1 0 0 170 45\n')
            with self.assertRaisesRegex(ValueError,'does not match'):fill_crosshair(game)


if __name__=="__main__":unittest.main()
