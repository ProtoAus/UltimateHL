"""Read-only 1998 PAK versus effective 3248 HUD/texture-source comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pak_entries(path):
    with path.open("rb") as file:
        magic, start, size = struct.unpack("<4sii", file.read(12))
        if magic != b"PACK" or start < 12 or size < 0 or size % 64 or start + size > path.stat().st_size:
            raise ValueError("Invalid PAK directory")
        file.seek(start)
        entries = {}
        for _ in range(size // 64):
            name, offset, length = struct.unpack("<56sii", file.read(64))
            if offset < 0 or length < 0 or offset + length > path.stat().st_size:
                raise ValueError("Invalid PAK entry")
            entries[name.split(b"\0", 1)[0].decode("ascii")] = (offset, length)
        return entries


def hud_rows(data):
    rows = {}
    for line in data.decode("ascii").splitlines():
        fields = line.split("//", 1)[0].split()
        if len(fields) == 7:
            rows[fields[0] + ":" + fields[1]] = fields[2:]
    return rows


def compare():
    pak = ROOT / "HL1998/valve/pak0.pak"
    entries = pak_entries(pak)
    mod = ROOT / "Half-Life 2005 WON/Half-Life/valve_WON"
    base = ROOT / "Half-Life 2005 WON/Half-Life/valve"
    assets = {}
    hud_old = hud_new = None
    with pak.open("rb") as file:
        for name, (offset, size) in entries.items():
            if not (name == "sprites/hud.txt" or name.startswith("sprites/") and
                    "hud" in name and name.endswith(".spr")):
                continue
            file.seek(offset)
            old = file.read(size)
            effective = mod / name if (mod / name).is_file() else base / name
            new = effective.read_bytes() if effective.is_file() else None
            assets[name] = {"won_sha256": digest(old), "steam_sha256": digest(new) if new else None,
                            "identical": old == new, "effective_path": effective.relative_to(ROOT).as_posix()}
            if name == "sprites/hud.txt":
                hud_old, hud_new = hud_rows(old), hud_rows(new) if new else {}
    changed = {name: {"won": value, "steam": hud_new.get(name)} for name, value in hud_old.items()
               if hud_new.get(name) != value}
    wads = {}
    for name in ("halflife.wad", "gfx.wad", "decals.wad", "liquids.wad", "xeno.wad"):
        old = (ROOT / "HL1998/valve" / name).read_bytes()
        effective = mod / name if (mod / name).is_file() else base / name
        if effective.is_file():
            new = effective.read_bytes()
            wads[name] = {"won_sha256": digest(old), "steam_sha256": digest(new), "identical": old == new}
    return {"warning": "Asset identity is not proof that render/upload/coordinate behavior is identical.",
            "hud_assets": assets, "changed_common_hud_rows": changed,
            "extra_steam_hud_rows": sorted(set(hud_new) - set(hud_old)),
            "won_hud_row_count": len(hud_old), "steam_hud_row_count": len(hud_new), "wad_files": wads}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    text = json.dumps(compare(), indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as file:
            file.write(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
