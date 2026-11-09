"""Exercise the real Windows loader on a PATCHED MOCK, never the game."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import pefile
from patch_client import build_client


def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else
                 Path(__file__).resolve().parents[1] / "build/Release").resolve()
    data = (build / "client.dll").read_bytes()
    pe = pefile.PE(data=data)
    mouse = next(e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name == b"mockMouseActive")
    with tempfile.TemporaryDirectory(prefix="uhl-loader-test-") as temp:
        folder = Path(temp)
        (folder / "client.dll").write_bytes(build_client(data, mouse))
        shutil.copy2(build / "UHLCompat.dll", folder / "UHLCompat.dll")
        subprocess.run([str(build / "mock_host.exe"), str(folder / "client.dll")], check=True)


if __name__ == "__main__":
    main()
