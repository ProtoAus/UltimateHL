"""Crosshair improvement packs for the valve_WON HUD.

  export          crosshairs.spr -> editable PNGs (whole sheet + one PNG per crosshair)
  pack            edited PNGs -> a new crosshairs.spr (byte-identical if nothing changed)
  install         make a packed crosshairs.spr the default "UltimateHL" style, keeping the
                  "Original" style selectable in the settings panel (--revert removes both)
  fill-crosshair  give weapons without a crosshair of their own (crowbar, grenade, satchel,
                  snark, tripmine) slot 1 of the sheet, or the glock's crosshair with
                  --glock (--revert restores the original weapon lists). BXT's
                  bxt_set_crosshair with no argument shows the same crosshair.

Only sprite/text assets are touched: no DLL or engine file is modified. Requires
Pillow (pip install -r requirements.txt). Restart the game after any change,
because crosshair sprites are cached at video initialization.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

from PIL import Image

HEADER_FORMAT = "<4s3if3ifi"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
SPR_NORMAL, SPR_ADDITIVE, SPR_INDEXALPHA, SPR_ALPHTEST = range(4)
TRANSPARENT = 255
SPRITE_NAME = "crosshairs"
CROSSHAIR_KINDS = ("crosshair", "autoaim", "zoom", "zoom_autoaim", "autoaim_c")
GLOCK = "weapon_9mmhandgun.txt"
SKIPPED_WEAPONS = {"weapon_question.txt"}  # placeholder entry, never selectable
# Weapons without a crosshair of their own: the crowbar and snark have none, and the
# thrown/placed weapons only borrow the gauss circle. They always get the fill.
NO_CROSSHAIR_WEAPONS = {"weapon_crowbar.txt", "weapon_handgrenade.txt", "weapon_satchel.txt",
                        "weapon_snark.txt", "weapon_tripmine.txt"}
BACKUP_FOLDER = "crosshair-originals"
STYLES = ("original", "ultimatehl")  # settings-panel order; must match native/settings.cpp
FILL_CELL = ("0", "0", "24", "24")  # slot 1 of crosshairs.spr


def pixels(image: Image.Image) -> list:
    # getdata() is deprecated from Pillow 12; keep older Pillow working too.
    return list(getattr(image, "get_flattened_data", image.getdata)())


class Sprite:
    """A single-frame GoldSrc (IDSP v2) sprite."""

    def __init__(self, data: bytes):
        if len(data) < HEADER_SIZE + 2:
            raise ValueError("File is too small to be a sprite")
        header = struct.unpack_from(HEADER_FORMAT, data)
        if header[0] != b"IDSP" or header[1] != 2:
            raise ValueError("Expected a Half-Life (IDSP version 2) sprite")
        self.header = list(header)
        self.texture_format = header[3]
        if header[7] != 1:  # ident, version, type, format, radius, width, height, frames, ...
            raise ValueError(f"Expected one frame, found {header[7]}")
        offset = HEADER_SIZE
        colors = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        if colors != 256:
            raise ValueError(f"Expected a 256-colour palette, found {colors}")
        self.palette = [tuple(data[offset + 3 * i:offset + 3 * i + 3]) for i in range(256)]
        offset += 768
        frame_type, origin_x, origin_y, self.width, self.height = struct.unpack_from("<5i", data, offset)
        if frame_type != 0:
            raise ValueError("Frame groups are not supported")
        offset += 20
        self.origin = (origin_x, origin_y)
        self.indices = bytearray(data[offset:offset + self.width * self.height])
        if len(self.indices) != self.width * self.height or offset + len(self.indices) != len(data):
            raise ValueError("Sprite frame size does not match the file")

    def rgba(self, index: int) -> tuple[int, int, int, int]:
        if self.texture_format == SPR_ALPHTEST:
            return (0, 0, 0, 0) if index == TRANSPARENT else (*self.palette[index], 255)
        if self.texture_format == SPR_INDEXALPHA:
            return (*self.palette[255], index)
        return (*self.palette[index], 255)

    def image(self) -> Image.Image:
        result = Image.new("RGBA", (self.width, self.height))
        result.putdata([self.rgba(i) for i in self.indices])
        return result

    def to_bytes(self) -> bytes:
        header = struct.pack(HEADER_FORMAT, *self.header)
        palette = b"".join(bytes(c) for c in self.palette)
        frame = struct.pack("<5i", 0, *self.origin, self.width, self.height)
        return header + struct.pack("<H", 256) + palette + frame + bytes(self.indices)


def normalize(pixel: tuple[int, ...], texture_format: int) -> tuple[int, ...]:
    """What the engine can actually show for an RGBA pixel."""
    r, g, b, a = pixel
    if texture_format == SPR_ALPHTEST:
        return (0, 0, 0, 0) if a < 128 else (r, g, b, 255)
    if texture_format == SPR_INDEXALPHA:
        return (0, 0, 0, a)
    return (r, g, b, 255)


def encode(sprite: Sprite, edited: Image.Image) -> Sprite:
    """Return a copy of `sprite` showing `edited`, keeping every unchanged index."""
    if edited.size != (sprite.width, sprite.height):
        raise ValueError(f"Edited sheet is {edited.size[0]}x{edited.size[1]}; it must stay "
                         f"{sprite.width}x{sprite.height} because weapon files refer to pixel positions")
    fmt = sprite.texture_format
    shown = [normalize(p, fmt) for p in pixels(edited.convert("RGBA"))]
    original = [normalize(sprite.rgba(i), fmt) for i in sprite.indices]
    result = Sprite(sprite.to_bytes())
    if fmt == SPR_INDEXALPHA:
        result.indices = bytearray(p[3] for p in shown)
        return result
    usable = range(255) if fmt == SPR_ALPHTEST else range(256)
    changed = [i for i, (new, old) in enumerate(zip(shown, original)) if new != old]
    kept = {sprite.indices[i] for i, (new, old) in enumerate(zip(shown, original)) if new == old}
    lookup = {}
    for index in usable:
        lookup.setdefault(sprite.palette[index], index)
    opaque_changed = {shown[i][:3] for i in changed if shown[i][3]}
    needed = opaque_changed - set(lookup)
    reused = {lookup[color] for color in opaque_changed if color in lookup}
    free = [i for i in usable if i not in kept and i not in reused]
    if len(needed) <= len(free):
        for color, index in zip(sorted(needed), free):
            if sprite.palette[index] in lookup and lookup[sprite.palette[index]] == index:
                del lookup[sprite.palette[index]]
            result.palette[index] = color
            lookup[color] = index
        for i in changed:
            result.indices[i] = lookup[shown[i][:3]] if shown[i][3] else TRANSPARENT
        return result
    # Too many new colours to keep the old palette: requantize the whole sheet.
    opaque = [p[:3] for p in shown if p[3]]
    filler = opaque[0] if opaque else (0, 0, 0)
    rgb = Image.new("RGB", edited.size)
    rgb.putdata([p[:3] if p[3] else filler for p in shown])
    quantized = rgb.quantize(colors=len(usable), method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat = quantized.getpalette()[:3 * len(usable)]
    for index in usable:
        if 3 * index + 2 < len(flat):
            result.palette[index] = tuple(flat[3 * index:3 * index + 3])
    result.indices = bytearray(TRANSPARENT if fmt == SPR_ALPHTEST and not p[3] else q
                               for p, q in zip(shown, pixels(quantized)))
    return result


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sprites_dirs(game: Path) -> list[Path]:
    return [game / "valve_WON/sprites", game / "valve/sprites"]


def find_sprite(game: Path) -> Path:
    for folder in sprites_dirs(game):
        if (folder / f"{SPRITE_NAME}.spr").is_file():
            return folder / f"{SPRITE_NAME}.spr"
    raise FileNotFoundError(f"No {SPRITE_NAME}.spr under {game}/valve_WON or {game}/valve")


def read_list(path: Path, strict: bool = True) -> tuple[list[bytes], int, list[tuple[int, list[str]]]]:
    return parse_list(path.read_bytes(), path, strict)


def parse_list(data: bytes, path: Path, strict: bool = True) -> tuple[list[bytes], int, list[tuple[int, list[str]]]]:
    """Parse a sprite list: raw lines, count-line index and (line index, fields) entries.

    The game reads only the first `count` entries. Files we rewrite must match
    exactly (strict); files we only read are truncated the way the game does.
    """
    lines = data.splitlines(keepends=True)
    count_line, entries = None, []
    for number, line in enumerate(lines):
        fields = line.decode("latin-1").split("//")[0].split()
        if not fields:
            continue
        if count_line is None:
            if len(fields) != 1 or not fields[0].isdigit():
                raise ValueError(f"{path}: first line must be the entry count")
            count_line = number
        elif len(fields) == 7:
            entries.append((number, fields))
        else:
            raise ValueError(f"{path}:{number + 1}: expected 7 fields")
    if count_line is None:
        raise ValueError(f"{path}: empty sprite list")
    count = int(lines[count_line].split()[0])
    if strict and count != len(entries):
        raise ValueError(f"{path}: entry count {count} does not match {len(entries)} entries")
    return lines, count_line, entries[:count]


def crosshair_cells(folder: Path) -> dict[tuple[int, int, int, int], list[str]]:
    """Every crosshairs.spr rectangle referenced by the weapon and HUD lists."""
    cells: dict[tuple[int, int, int, int], list[str]] = {}
    for path in sorted(folder.glob("weapon_*.txt")) + [folder / "hud.txt"]:
        if not path.is_file():
            continue
        for _, (name, res, sprite, *rect) in read_list(path, strict=False)[2]:
            if sprite == SPRITE_NAME and name in CROSSHAIR_KINDS:
                cells.setdefault(tuple(map(int, rect)), []).append(f"{path.stem} {name} {res}")
    return cells


def cell_name(users: list[str]) -> str:
    stem, kind, _ = users[0].split()
    return f"{stem.removeprefix('weapon_')}_{kind}"


def export(game: Path, out: Path) -> dict:
    if out.exists() and any(out.iterdir()):
        raise ValueError(f"{out} is not empty; export into a new folder")
    source = find_sprite(game)
    data = source.read_bytes()
    sprite = Sprite(data)
    folder = next((f for f in sprites_dirs(game) if (f / GLOCK).is_file()), None)
    if folder is None:
        raise FileNotFoundError(f"No {GLOCK} under {game}/valve_WON or {game}/valve")
    (out / "cells").mkdir(parents=True)
    (out / "source.spr").write_bytes(data)
    sheet = sprite.image()
    sheet.save(out / f"{SPRITE_NAME}.png")
    cells = []
    for rect, users in sorted(crosshair_cells(folder).items(), key=lambda item: cell_name(item[1])):
        x, y, w, h = rect
        if x < 0 or y < 0 or x + w > sprite.width or y + h > sprite.height:
            raise ValueError(f"{users[0]} uses a rectangle outside the sheet")
        name = cell_name(users)
        if any(c["file"] == f"cells/{name}.png" for c in cells):
            name = f"{name}_{x}_{y}"
        sheet.crop((x, y, x + w, y + h)).save(out / "cells" / f"{name}.png")
        cells.append({"file": f"cells/{name}.png", "rect": list(rect), "used_by": users})
    manifest = {"source": str(source), "source_sha256": sha256(data),
                "texture_format": sprite.texture_format, "size": [sprite.width, sprite.height], "cells": cells}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2))
    return {"sheet": str(out / f"{SPRITE_NAME}.png"), "cells": len(cells)}


def pack(folder: Path, destination: Path) -> dict:
    manifest = json.loads((folder / "manifest.json").read_text())
    data = (folder / "source.spr").read_bytes()
    if sha256(data) != manifest["source_sha256"]:
        raise ValueError("source.spr does not match manifest.json; re-export")
    if destination.exists():
        raise ValueError(f"{destination} already exists; choose a new file")
    sprite = Sprite(data)
    fmt = sprite.texture_format
    original = sprite.image()
    sheet = Image.open(folder / f"{SPRITE_NAME}.png").convert("RGBA")
    if sheet.size != original.size:
        raise ValueError(f"{SPRITE_NAME}.png must stay {original.size[0]}x{original.size[1]}")
    pasted: list[tuple[str, tuple[int, int, int, int]]] = []
    for cell in manifest["cells"]:
        x, y, w, h = cell["rect"]
        edited = Image.open(folder / cell["file"]).convert("RGBA")
        if edited.size != (w, h):
            raise ValueError(f"{cell['file']} must stay {w}x{h}")
        before = [normalize(p, fmt) for p in pixels(original.crop((x, y, x + w, y + h)))]
        if [normalize(p, fmt) for p in pixels(edited)] == before:
            continue
        for other, (ox, oy, ow, oh) in pasted:
            if x < ox + ow and ox < x + w and y < oy + oh and oy < y + h:
                raise ValueError(f"{cell['file']} and {other} overlap and were both edited")
        sheet.paste(edited, (x, y))
        pasted.append((cell["file"], (x, y, w, h)))
    output = encode(sprite, sheet).to_bytes()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as file:
        file.write(output)
    return {"output": str(destination), "sha256": sha256(output),
            "unchanged": output == data, "edited_cells": [name for name, _ in pasted]}


def style_path(game: Path, style: str) -> Path:
    """Where the settings panel copies a crosshair style from."""
    return game.parent / "HUD-presets" / SPRITE_NAME / style / f"{SPRITE_NAME}.spr"


def install(sprite_path: Path, game: Path, revert: bool = False) -> dict:
    target = game / "valve_WON/sprites" / f"{SPRITE_NAME}.spr"
    backup = game.parent / BACKUP_FOLDER / "installed" / f"{SPRITE_NAME}.spr"
    if revert:
        for style in STYLES:
            path = style_path(game, style)
            path.unlink(missing_ok=True)
            for folder in (path.parent, path.parent.parent):
                if folder.is_dir() and not any(folder.iterdir()):
                    folder.rmdir()
        if backup.is_file():
            shutil.copyfile(backup, target)
            backup.unlink()
            return {"restored": str(target)}
        target.unlink(missing_ok=True)
        return {"removed": str(target)}
    data = sprite_path.read_bytes()
    Sprite(data)  # refuse anything that is not a valid sprite
    original = (game / "valve/sprites" / f"{SPRITE_NAME}.spr").read_bytes()
    Sprite(original)
    known = {original, data} | {style_path(game, s).read_bytes() for s in STYLES if style_path(game, s).is_file()}
    if target.is_file() and target.read_bytes() not in known and not backup.is_file():
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(target, backup)  # a hand-installed sheet: keep it for --revert
    for style, content in zip(STYLES, (original, data)):
        style_path(game, style).parent.mkdir(parents=True, exist_ok=True)
        style_path(game, style).write_bytes(content)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return {"installed": str(target), "default_style": STYLES[1],
            "styles": str(style_path(game, STYLES[1]).parent.parent), "sha256": sha256(data)}


def list_folders(game: Path) -> dict[str, Path]:
    """The live sprite folder plus every HUD preset the settings panel copies from."""
    folders = {"live": game / "valve_WON/sprites"}
    presets = game.parent / "HUD-presets"
    if presets.is_dir():
        for preset in sorted(presets.iterdir()):
            if preset.is_dir() and (preset / GLOCK).is_file():
                folders[f"preset-{preset.name}"] = preset
    return folders


def filled(data: bytes, path: Path, fill: dict[str, bytes], blank) -> bytes:
    """A weapon list with `fill` crosshairs if it has none of its own."""
    lines, count_line, entries = parse_list(data, path)
    crosshairs = [(number, res, sprite, rect)
                  for number, (name, res, sprite, *rect) in entries if name == "crosshair"]
    if path.name not in NO_CROSSHAIR_WEAPONS and any(not blank(sprite, rect) for _, _, sprite, rect in crosshairs):
        return data  # already shows a crosshair at some resolution
    newline = b"\r\n" if lines[count_line].endswith(b"\r\n") else b"\n"
    missing = dict(fill)
    replaced = False
    for number, res, _, _ in crosshairs:
        if res in missing:
            lines[number] = missing.pop(res) + newline
            replaced = True
    if not replaced and not missing:
        return data
    if not lines[-1].endswith((b"\n", b"\r")):
        lines[-1] += newline
    lines.extend(line + newline for line in missing.values())
    lines[count_line] = str(len(entries) + len(missing)).encode() + newline
    return b"".join(lines)


def fill_crosshair(game: Path, glock: bool = False, revert: bool = False) -> dict:
    """Always rebuilt from the saved originals, so switching fill or reverting is exact."""
    backups = game.parent / BACKUP_FOLDER
    stock = game / "valve/sprites" / f"{SPRITE_NAME}.spr"
    # Judge "no crosshair" on the stock sheet so the answer does not change with the style.
    sheet = Sprite((stock if stock.is_file() else find_sprite(game)).read_bytes())

    def blank(sprite: str, rect: list[str]) -> bool:
        """True if the entry points at an empty crosshairs.spr cell (draws nothing)."""
        if sprite != SPRITE_NAME or sheet.texture_format != SPR_ALPHTEST:
            return False
        x, y, w, h = map(int, rect)
        return all(sheet.indices[(y + row) * sheet.width + x + col] == TRANSPARENT
                   for row in range(h) for col in range(w))

    changed, restored = [], []
    for label, folder in list_folders(game).items():
        if glock:
            glock_lines, _, glock_entries = read_list(folder / GLOCK)
            fill = {fields[1]: glock_lines[number].rstrip(b"\r\n")
                    for number, fields in glock_entries if fields[0] == "crosshair"}
        else:
            cell = "\t".join(FILL_CELL)
            fill = {res: f"crosshair\t\t{res} {SPRITE_NAME}\t{cell}".encode() for res in ("320", "640")}
        for path in sorted(folder.glob("weapon_*.txt")):
            backup = backups / label / path.name
            original = backup.read_bytes() if backup.is_file() else path.read_bytes()
            skip = revert or path.name in SKIPPED_WEAPONS or path.name == GLOCK
            updated = original if skip else filled(original, path, fill, blank)
            if updated == original:
                if backup.is_file():
                    path.write_bytes(original)
                    backup.unlink()
                    restored.append(f"{label}/{path.name}")
                continue
            if path.read_bytes() == updated:
                continue
            if not backup.is_file():
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, backup)
            path.write_bytes(updated)
            changed.append(f"{label}/{path.name}")
        if (backups / label).is_dir() and not any((backups / label).iterdir()):
            (backups / label).rmdir()
    return {"changed": changed, "restored": restored}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("export", help="crosshairs.spr -> PNGs")
    command.add_argument("game", type=Path, help="the Half-Life folder (contains valve_WON)")
    command.add_argument("out", type=Path, help="new, empty folder for the PNGs")
    command = commands.add_parser("pack", help="edited PNGs -> crosshairs.spr")
    command.add_argument("folder", type=Path, help="a folder made by export")
    command.add_argument("destination", type=Path, help="new .spr file to write")
    command = commands.add_parser("install", help="make a packed .spr the default crosshair style")
    command.add_argument("sprite", type=Path, nargs="?")
    command.add_argument("game", type=Path)
    command.add_argument("--revert", action="store_true")
    command = commands.add_parser("fill-crosshair", help="crosshair for weapons without one")
    command.add_argument("game", type=Path)
    command.add_argument("--glock", action="store_true", help="use the glock's crosshair instead of slot 1")
    command.add_argument("--revert", action="store_true")
    args = parser.parse_args()
    if args.command == "export":
        result = export(args.game, args.out)
    elif args.command == "pack":
        result = pack(args.folder, args.destination)
    elif args.command == "install":
        if not args.revert and args.sprite is None:
            parser.error("install needs a sprite unless --revert is given")
        result = install(args.sprite, args.game, args.revert)
    else:
        result = fill_crosshair(args.game, args.glock, args.revert)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
