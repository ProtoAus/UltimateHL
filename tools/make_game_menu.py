"""Add a menu link to the owned copy, without changing the original GameUI DLL."""
from pathlib import Path


def install(game: Path):
    source=game/'valve/resource/GameMenu.res'
    text=source.read_text(encoding='utf-8-sig')
    if '"GameMenu"' not in text or text.rfind('}')<0:raise ValueError('Unknown GameMenu resource')
    index=text.rfind('}')
    entry='\n\t"UltimateHL"\n\t{\n\t\t"label" "UltimateHL settings..."\n\t\t"command" "engine uhl_settings"\n\t}\n'
    target=game/'valve_WON/resource/GameMenu.res'
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(text[:index]+entry+text[index:],encoding='utf-8')
    return target
