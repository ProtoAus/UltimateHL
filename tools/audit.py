"""Read-only fingerprints and BXT command-name comparison for this workspace.

Strings indicate possible features, not proof that a command is registered or
that two implementations are equivalent. No game/DLL is loaded or executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parent
BXT_FILES = {
    "bundled_2023": "Half-Life 2005 WON/Bunnymod XT/BunnymodXT.dll",
    "release": "BXT-Windows/release/BunnymodXT.dll",
    "dev": "BXT-Windows/dev/BunnymodXT.dll",
    "tf": "BXT-Windows/tf/BunnymodXT_tf.dll",
}
CRITICAL_FILES = [
    "HL1998/hl.exe", "HL1998/hw.dll",
    "Half-Life 2005 WON/Half-Life/hl.exe",
    "Half-Life 2005 WON/Half-Life/hw.dll",
    "Half-Life 2005 WON/Half-Life/hw.dll.original",
    "Half-Life 2005 WON/Half-Life/valve_WON/cl_dlls/client.dll",
    "Half-Life 2005 WON/Half-Life/valve_WON/dlls/hl.dll",
    "Half-Life 2005 WON/RInput/RInput.dll",
]


def fingerprint(relative):
    path = ROOT / relative
    if not path.is_file():
        return {"path": relative, "missing": True}
    data = path.read_bytes()
    return {"path": relative, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "mz_header": data[:2] == b"MZ"}


def pak_directory(path):
    """List only HUD/sprite entries; don't unpack or modify a Valve archive."""
    entries = []
    with path.open("rb") as file:
        header = file.read(12)
        if len(header) != 12 or header[:4] != b"PACK":
            return entries
        offset, size = struct.unpack("<ii", header[4:])
        if offset < 12 or size < 0 or size % 64 or offset + size > path.stat().st_size:
            return entries
        file.seek(offset)
        for _ in range(size // 64):
            entry = file.read(64)
            name, start, length = struct.unpack("<56sii", entry)
            name = name.split(b"\0", 1)[0].decode("ascii", errors="replace")
            if name.startswith("sprites/") and (name.endswith("hud.txt") or name.endswith(".spr")):
                entries.append({"name": name, "bytes": length})
    return entries


def audit():
    bx = {}
    for name, relative in BXT_FILES.items():
        record = fingerprint(relative)
        if not record.get("missing"):
            data = (ROOT / relative).read_bytes()
            record["version_strings"] = [m.decode("ascii") for m in
                re.findall(rb"Bunnymod XT version [ -~]+", data)]
            record["bxt_names"] = sorted({m.decode("ascii") for m in
                re.findall(rb"(?<![A-Za-z0-9_])(_?bxt_[a-z0-9_]+)\x00", data)})
        bx[name] = record
    release = set(bx["release"].get("bxt_names", []))
    for record in bx.values():
        names = set(record.get("bxt_names", []))
        record["names_extra_vs_release"] = sorted(names - release)
        record["names_missing_vs_release"] = sorted(release - names)
    source = ROOT / "BunnymodXT-sep-19-2026"
    dependencies = {name: any((source / name).rglob("*.*")) for name in
                    ("SPTLib", "hlstrafe", "taslogger", "cereal", "rapidjson", "discord-rpc")}
    saves = {}
    for directory in [ROOT / "HL1998/valve", ROOT / "Half-Life 2005 WON/Half-Life/valve_WON"]:
        saves[directory.relative_to(ROOT).as_posix()] = [p.name for p in directory.rglob("*")
                                                         if p.is_file() and p.suffix.lower() == ".sav"]
    assets = {}
    for directory in [ROOT / "HL1998/valve", ROOT / "Half-Life 2005 WON/Half-Life/valve"]:
        for pak in directory.glob("*.pak"):
            assets[pak.relative_to(ROOT).as_posix()] = pak_directory(pak)
    return {"warning": "Read-only static audit, not a gameplay test. Names are not implementation equivalence.",
            "critical_files": [fingerprint(path) for path in CRITICAL_FILES],
            "bxt": bx, "local_source": {"path": source.name, "has_git_metadata": (source / ".git").exists(),
                                           "dependency_has_files": dependencies},
            "save_files": saves, "pak_hud_sprite_entries": assets}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Optional NEW output file; otherwise stdout")
    args = parser.parse_args()
    text = json.dumps(audit(), indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as file:
            file.write(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
