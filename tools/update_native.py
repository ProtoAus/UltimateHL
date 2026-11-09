"""Update a known v3 native DLL/launcher without replacing saves or configs.

Close the game first. Refuses changed engine/client/adapter/launcher hashes.
Keeps hash-named backups and atomically replaces each binary; rolls back prior
binary replacements if a later replacement fails. Never writes Windows settings.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

import pefile
from patch_client import ADAPTERS, CLIENT_SHA256, sha256

PROJECT = Path(__file__).resolve().parents[1]


def update(destination: Path, native: Path):
    game = destination / "Half-Life"
    path = destination / "stage-manifest.json"
    manifest = json.loads(path.read_text())
    if manifest.get("adapter_version") != 3:
        raise ValueError("Only a v3 playtest can be updated by this source; stage a NEW copy for other versions")
    expected = dict(manifest["unchanged_baseline_files"])
    # Either the client patched on disk, or the unmodified client that BXT adapts at runtime.
    if sha256((game / "valve_WON/cl_dlls/client.dll").read_bytes()) not in (manifest["client"]["patched_sha256"], CLIENT_SHA256):
        raise ValueError("Changed valve_WON/cl_dlls/client.dll; refusing to overwrite an unknown installation")
    expected["UHLCompat.dll"] = manifest["native_sha256"]
    expected["hl.exe.manifest"] = manifest["dpi_manifest_sha256"]
    for relative, digest in expected.items():
        if sha256((game / relative).read_bytes()) != digest:
            raise ValueError(f"Changed {relative}; refusing to overwrite an unknown installation")
    launcher = native.with_name("UltimateHL.exe")
    if sha256((destination / "UltimateHL.exe").read_bytes()) != manifest["launcher_sha256"]:
        raise ValueError("Changed UltimateHL.exe; refusing to overwrite an unknown launcher")
    pe = pefile.PE(str(native))
    exports = {e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    if pe.FILE_HEADER.Machine != 0x14C or not set(ADAPTERS.values()).issubset(exports):
        raise ValueError("Expected the 32-bit v3 adapter build")
    if pefile.PE(str(launcher)).FILE_HEADER.Machine != 0x14C:
        raise ValueError("Expected the 32-bit v3 launcher build")
    plans = []
    backups = destination / "adapter-backups"
    for key, source, target in (("native_sha256", native, game / "UHLCompat.dll"),
                                ("launcher_sha256", launcher, destination / "UltimateHL.exe")):
        data = source.read_bytes()
        digest = sha256(data)
        if digest == manifest[key]:
            continue
        backup = backups / f"{target.stem}-{manifest[key]}{target.suffix}"
        backups.mkdir(exist_ok=True)
        if not backup.exists():
            shutil.copy2(target, backup)
        elif sha256(backup.read_bytes()) != manifest[key]:
            raise ValueError(f"Backup exists with unexpected content: {backup}")
        manifest.setdefault(f"previous_{key}", []).append(manifest[key])
        manifest[key] = digest
        plans.append((target, backup, data))
    if not plans:
        print("Native DLL and launcher already match this build; no files changed.")
        return
    manifest["windows_mouse_settings_policy"] = "client SPI_SETMOUSE/SPI_SETMOUSESPEED blocked before initialization through shutdown; no cached restore"
    temporary = []
    replaced = []
    def prepare(target: Path, data: bytes):
        with tempfile.NamedTemporaryFile(dir=target.parent, suffix=target.suffix, delete=False) as file:
            name = Path(file.name)
            temporary.append(name)
            file.write(data)
        return name
    try:
        binaries = [(target, backup, prepare(target, data)) for target, backup, data in plans]
        metadata = prepare(path, (json.dumps(manifest, indent=2) + "\n").encode())
        try:
            for target, backup, staged in binaries:
                os.replace(staged, target)
                replaced.append((target, backup))
            os.replace(metadata, path)
        except Exception:
            for target, backup in reversed(replaced):
                os.replace(prepare(target, backup.read_bytes()), target)
            raise
    finally:
        for staged in temporary:
            if staged.exists():
                staged.unlink()
    print(f"Updated native binaries: {destination}. Backups kept; saves/configs untouched.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, nargs="?", default=PROJECT / "playtest-v3.6")
    parser.add_argument("--native-dll", type=Path, default=PROJECT / "build/Release/UHLCompat.dll")
    args = parser.parse_args()
    update(args.destination, args.native_dll)


if __name__ == "__main__":
    main()
