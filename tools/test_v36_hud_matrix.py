"""Owned game-only 640/AG HUD captures. No desktop capture, input, or focus injection."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
from PIL import Image
from test_v32_offline import running_games


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('installation',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--allow-launch',action='store_true');args=parser.parse_args();root=args.installation.resolve()
    if not args.allow_launch:raise ValueError('Explicit --allow-launch required')
    if json.loads((root/'stage-manifest.json').read_text()).get('build_version')!='3.6':raise ValueError('Only the separately staged v3.6 build is allowed')
    if args.output.exists():raise ValueError('Choose a new output directory')
    if running_games():raise ValueError('An existing hl.exe was left alone; no test launched')
    args.output.mkdir(parents=True);game=root/'Half-Life';sprites=game/'valve_WON/sprites'
    before={p:(p.read_bytes(),p.stat())for p in sprites.glob('*.txt')};result=[]
    try:
        for preset,scale in (('stock',1),('stock',1.5),('stock',2),('ag',1)):
            if running_games():raise ValueError('Another game appeared; matrix stopped')
            for path in (root/'HUD-presets'/preset).glob('*.txt'):shutil.copy2(path,sprites/path.name)
            tag=f'{preset}-{scale}';report=args.output/(tag+'.json');frames=set(game.glob('uhl-frame*.bmp'))
            subprocess.run([sys.executable,str(Path(__file__).with_name('test_v32_offline.py')),str(root),str(report),
                '--allow-launch','--frame-dump','--hud-scale',str(scale),'--bxt-features-test'],check=True,timeout=75)
            new=set(game.glob('uhl-frame*.bmp'))-frames
            if len(new)!=1:raise RuntimeError('Expected exactly one owned framebuffer capture')
            image=Image.open(new.pop());image.save(args.output/(tag+'.png'));w,h=image.size
            image.crop((0,h-160,w,h)).save(args.output/(tag+'-hud.png'))
            text=json.loads(report.read_text())['log']
            expected=('bxt_hud_entities_min','bxt_hud_entities_max','bxt_hud_entities_avel','bxt_ch_rot_ft','bxt_show_rotate_trace',
                'bxt_hud_rotate_trace','bxt_hud_physents','bxt_ch_set_groundentity','bxt_ch_rot_ent','bxt_ch_rot_trace')
            present={name: name in text and f'Unknown command: {name}'not in text for name in expected}
            result.append(dict(preset=preset,scale=scale,image_size=image.size,features=present,
                hud_status=[line for line in text.splitlines()if line.startswith('HUD canvas:')]))
    finally:
        for path in sprites.glob('*.txt'):
            if path not in before:path.unlink()
        for path,(data,stat)in before.items():
            path.write_bytes(data);os.utime(path,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        (args.output/'matrix.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()
