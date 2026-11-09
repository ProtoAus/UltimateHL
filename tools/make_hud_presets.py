"""Install reversible native HUD art, without resampling sprite bitmaps."""
from pathlib import Path
import argparse
import json
import re
import shutil
import struct

KEEP_640={"crosshair","autoaim","zoom","zoom_autoaim","autoaim_c"}

def compact_text(text: str) -> tuple[str,int]:
    rows={}
    for line in text.splitlines():
        fields=line.split()
        if len(fields)==7 and fields[1]=='320':rows[fields[0]]=fields
    output=[];changed=0
    for line in text.splitlines(keepends=True):
        fields=line.split()
        if len(fields)==7 and fields[1]=='640' and fields[0]not in KEEP_640 and fields[0]in rows:
            fields=rows[fields[0]].copy();fields[1]='640';line='\t'.join(fields)+'\n';changed+=1
        output.append(line)
    return ''.join(output),changed

def validate_ag(game: Path, source: Path) -> dict:
    listing=source/'hud.txt'
    if not listing.is_file():raise ValueError(f'AG HUD list missing: {listing}')
    references={}
    for line in listing.read_text(encoding='latin-1').splitlines():
        fields=line.split('//',1)[0].split()
        if len(fields)!=7:continue
        name,res,sprite,x,y,width,height=fields
        if not re.fullmatch(r'[A-Za-z0-9_-]+',sprite):raise ValueError('Unsafe AG sprite name')
        coords=tuple(map(int,(x,y,width,height)))
        candidates=(source/(sprite+'.spr'),game/'valve_WON/sprites'/(sprite+'.spr'),game/'valve/sprites'/(sprite+'.spr'))
        asset=next((p for p in candidates if p.is_file()),None)
        if asset is None:raise ValueError(f'AG HUD sprite missing: {sprite}.spr')
        raw=asset.read_bytes()
        if len(raw)<42 or raw[:4]!=b'IDSP' or struct.unpack_from('<i',raw,4)[0]!=2:raise ValueError(f'Unsupported AG sprite: {asset.name}')
        atlas_w,atlas_h=struct.unpack_from('<2i',raw,20)
        if min(coords)<0 or width=='0' or height=='0' or coords[0]+coords[2]>atlas_w or coords[1]+coords[3]>atlas_h:
            raise ValueError(f'AG crop outside {asset.name}: {name}')
        references[sprite]=dict(path=str(asset),width=atlas_w,height=atlas_h)
    if not references:raise ValueError('AG HUD has no sprite definitions')
    return references

def make(game: Path,presets: Path,selected: str='compact',ag_source: Path|None=None) -> dict:
    allowed=('compact','stock','ag')if ag_source else('compact','stock')
    if selected not in allowed:raise ValueError('Unknown/unavailable HUD preset')
    if presets.exists():raise ValueError('HUD preset output already exists')
    ag_refs=validate_ag(game,ag_source)if ag_source else None
    files={p.name:p for p in (game/'valve/sprites').glob('*.txt')}
    files.update({p.name:p for p in (game/'valve_WON/sprites').glob('*.txt')})
    summary={}
    for name,path in files.items():
        text=path.read_bytes().decode('latin-1');compact,count=compact_text(text)
        if not count:continue
        summary[name]=count
        for preset,value in (('stock',text),('compact',compact)):
            out=presets/preset/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(value.encode('latin-1'))
    if 'hud.txt'not in summary:raise ValueError('No stock 320/640 HUD lists found')
    if ag_source:
        shutil.copytree(presets/'stock',presets/'ag')
        shutil.copy2(ag_source/'hud.txt',presets/'ag/hud.txt')
        # Unique AG sprite names stay installed when switching presets. Stock
        # lookups never reference them; no destructive delete/rewrite is needed.
        for asset in ag_source.glob('*.spr'):
            dest=game/'valve_WON/sprites'/asset.name;dest.parent.mkdir(parents=True,exist_ok=True)
            if dest.exists()and dest.read_bytes()!=asset.read_bytes():raise ValueError(f'AG asset collision: {asset.name}')
            shutil.copy2(asset,dest)
    target=game/'valve_WON/sprites';target.mkdir(parents=True,exist_ok=True)
    for path in (presets/selected).glob('*.txt'):shutil.copy2(path,target/path.name)
    (presets/'presets.json').write_text(json.dumps(dict(selected=selected,lists=summary,ag_references=ag_refs),indent=2)+'\n')
    return summary

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('game',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--selected',choices=('compact','stock','ag'),default='stock');parser.add_argument('--ag-source',type=Path)
    args=parser.parse_args();print(make(args.game,args.output,args.selected,args.ag_source))
