"""Switch an installed copy between the legit-run layout and the patched client.

  stock    the unmodified client.dll; the rebuilt BXT attaches UHLCompat.dll at runtime
  patched  the client.dll that imports UHLCompat.dll itself (works without BXT)

Both directions are verified by SHA-256 and keep the replaced file in adapter-backups.
Saves and configs are never touched. Close the game first.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile

from patch_client import CLIENT_SHA256, MOUSE_ACTIVE_RVA, build_client, sha256

CLIENT = "Half-Life/valve_WON/cl_dlls/client.dll"
ATTACH_MARKER = b"UHL_AttachClient"  # present only in a BXT built with the runtime attach


def switch(copy: Path, layout: str, stock: Path | None = None, stock_sha: str = CLIENT_SHA256) -> dict:
    if layout not in ("stock", "patched"):
        raise ValueError("Layout must be stock or patched")
    patched_sha = json.loads((copy / "stage-manifest.json").read_text())["client"]["patched_sha256"]
    target, backups = copy / CLIENT, copy / "adapter-backups"
    current = target.read_bytes()
    current_sha = sha256(current)
    if current_sha not in (patched_sha, stock_sha):
        raise ValueError("Unknown client.dll; refusing to replace it")
    wanted_sha = stock_sha if layout == "stock" else patched_sha
    if current_sha == wanted_sha:
        return {"layout": layout, "changed": False}
    if layout == "stock" and ATTACH_MARKER not in (copy / "BXT/BunnymodXT.dll").read_bytes():
        raise ValueError("This copy's BXT cannot attach UltimateHL; install the rebuilt BXT first")

    saved = backups / f"client-{wanted_sha}.dll"
    if saved.is_file():
        data = saved.read_bytes()
    elif layout == "stock":
        if stock is None:
            raise ValueError("Give --stock-client (the original valve_WON/cl_dlls/client.dll)")
        data = stock.read_bytes()
    else:
        data = build_client(current, MOUSE_ACTIVE_RVA)  # current is the stock client here
    if sha256(data) != wanted_sha:
        raise ValueError(f"Source for the {layout} client has the wrong SHA-256")

    backups.mkdir(exist_ok=True)
    keep = backups / f"client-{current_sha}.dll"
    if not keep.exists():
        keep.write_bytes(current)
    with tempfile.NamedTemporaryFile(dir=target.parent, suffix=".dll", delete=False) as file:
        file.write(data)
    os.replace(file.name, target)
    return {"layout": layout, "changed": True, "client_sha256": wanted_sha, "previous_kept": str(keep)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("layout", choices=("stock", "patched"))
    parser.add_argument("copy", type=Path, help="installed copy (contains Half-Life and stage-manifest.json)")
    parser.add_argument("--stock-client", type=Path, help="original valve_WON/cl_dlls/client.dll")
    args = parser.parse_args()
    print(json.dumps(switch(args.copy, args.layout, args.stock_client), indent=2))


if __name__ == "__main__":
    main()
