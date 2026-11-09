"""Read-only inspection of an explicitly selected staged hl.exe. Never injects."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct


class Module(c.Structure):
    _fields_ = [("size", w.DWORD), ("id", w.DWORD), ("pid", w.DWORD),
                ("global_refs", w.DWORD), ("refs", w.DWORD),
                ("base", c.POINTER(c.c_ubyte)), ("length", w.DWORD),
                ("handle", w.HMODULE), ("name", w.WCHAR * 256), ("path", w.WCHAR * 260)]


class MemoryInfo(c.Structure):
    _fields_ = [("base", c.c_void_p), ("allocation", c.c_void_p), ("allocation_protection", w.DWORD),
                ("size", c.c_size_t), ("state", w.DWORD), ("protection", w.DWORD), ("type", w.DWORD)]


class Reader:
    def __init__(self, pid, installation):
        self.kernel = k = c.WinDLL("kernel32", use_last_error=True)
        k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
        k.CloseHandle.argtypes = [w.HANDLE]
        k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]; k.CreateToolhelp32Snapshot.restype = w.HANDLE
        k.Module32FirstW.argtypes = k.Module32NextW.argtypes = [w.HANDLE, c.POINTER(Module)]
        k.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(MemoryInfo), c.c_size_t]
        k.VirtualQueryEx.restype = c.c_size_t
        self.handle = k.OpenProcess(0x410, False, pid)  # QUERY_INFORMATION | VM_READ only.
        if not self.handle: raise c.WinError(c.get_last_error())
        self.modules = {}
        snapshot = k.CreateToolhelp32Snapshot(0x18, pid)
        entry = Module(); entry.size = c.sizeof(entry)
        ok = k.Module32FirstW(snapshot, c.byref(entry))
        while ok:
            self.modules[entry.name.lower()] = (c.cast(entry.base, c.c_void_p).value, entry.length, entry.path)
            ok = k.Module32NextW(snapshot, c.byref(entry))
        k.CloseHandle(snapshot)
        expected = Path(installation).resolve() / "Half-Life/hl.exe"
        if "hl.exe" not in self.modules or Path(self.modules["hl.exe"][2]).resolve() != expected:
            self.close(); raise ValueError("PID is not the explicitly selected staged game")
        if not (Path(installation) / "stage-manifest.json").is_file():
            self.close(); raise ValueError("Not a staged installation")

    def read(self, address, size):
        result = c.create_string_buffer(size); read = c.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, c.c_void_p(address), result, size, c.byref(read)):
            raise c.WinError(c.get_last_error())
        return result.raw[:read.value]

    def engine(self):
        base, size, _ = self.modules["hl.exe"]
        result = bytearray(size); address = base
        while address < base + size:
            info = MemoryInfo()
            if not self.kernel.VirtualQueryEx(self.handle, c.c_void_p(address), c.byref(info), c.sizeof(info)): break
            end = min(base + size, info.base + info.size)
            if end <= address: break
            if info.state == 0x1000 and not info.protection & (0x100 | 1):
                try: result[address-base:end-base] = self.read(address, end-address)
                except OSError: pass
            address = end
        return base, bytes(result)  # Caller must not persist a whole data/credential dump.

    def api(self):
        # Exact supported WON client's original engine API copy; unchanged by adapter.
        return struct.unpack("<87I", self.read(self.modules["client.dll"][0] + 0x67078, 87 * 4))

    def close(self):
        if self.handle: self.kernel.CloseHandle(self.handle); self.handle = None

    def __enter__(self): return self
    def __exit__(self, *_): self.close()
