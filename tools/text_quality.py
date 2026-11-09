"""Smooth engine text: an antialiased EngineFont, selectable in the settings panel.

  install   write HUD-presets/fonts/{original,smooth}/TrackerScheme.res and make smooth active
  --revert  put the original scheme back and remove the presets

The engine draws its own text (centre prints, "Saved..."-style messages, net graph) with
EngineFont from platform/resource/TrackerScheme.res, which has no antialias flag. Only that
flag is added to each of its sizes; every other font and line is left as it is. Fonts are
built at startup, so restart the game after a change.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

SCHEME = "platform/resource/TrackerScheme.res"
STYLES = ("original", "smooth")  # settings-panel order; must match native/settings.cpp


def style_path(game: Path, style: str) -> Path:
    return game.parent / "HUD-presets" / "fonts" / style / "TrackerScheme.res"


def smooth(text: str) -> str:
    """Add "antialias" "1" to every size of the EngineFont block that lacks it."""
    start = re.search(r'"EngineFont"\s*\{', text)
    if not start:
        raise ValueError("No EngineFont in the scheme")
    depth, i = 0, start.end() - 1
    while True:  # find the block's closing brace
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            break
        i += 1
    block = text[start.start():i + 1]
    newline = "\r\n" if "\r\n" in text else "\n"

    def add(size: re.Match) -> str:
        body = size.group(0)
        if '"antialias"' in body:
            return body
        indent = re.search(r"\n([ \t]*)\"", body)  # the indentation of the size's settings
        pad = indent.group(1) if indent else "\t\t\t\t"
        head = body[:body.rfind("}")].rstrip(" \t")  # up to the line holding the closing brace
        return head + pad + '"antialias"\t"1"' + newline + body[len(head):]

    smoothed = re.sub(r'"\d+"\s*\{[^{}]*\}', add, block)
    return text[:start.start()] + smoothed + text[i + 1:]


def install(game: Path, revert: bool = False) -> dict:
    live = game / SCHEME
    if revert:
        original = style_path(game, "original")
        if original.is_file():
            live.write_bytes(original.read_bytes())
        for style in STYLES:
            path = style_path(game, style)
            path.unlink(missing_ok=True)
            for folder in (path.parent, path.parent.parent):
                if folder.is_dir() and not any(folder.iterdir()):
                    folder.rmdir()
        return {"restored": str(live)}
    original = style_path(game, "original")
    data = original.read_bytes() if original.is_file() else live.read_bytes()
    smoothed = smooth(data.decode("latin-1")).encode("latin-1")
    for style, content in zip(STYLES, (data, smoothed)):
        style_path(game, style).parent.mkdir(parents=True, exist_ok=True)
        style_path(game, style).write_bytes(content)
    live.write_bytes(smoothed)
    return {"installed": str(live), "style": "smooth"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", type=Path, help="the Half-Life folder (contains platform)")
    parser.add_argument("--revert", action="store_true")
    args = parser.parse_args()
    print(json.dumps(install(args.game, args.revert), indent=2))


if __name__ == "__main__":
    main()
