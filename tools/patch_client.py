"""Add a native import and nine export adapters to ONE known GoldSrc client.

Never edits in place. Does not decrypt/modify hw.dll, disable Steam, or change
client movement instructions. Requires pefile (pip install -r requirements.txt).
The low-level builder is also used by tests with a locally compiled mock client.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import pefile

CLIENT_SHA256 = "d294569c06768eb819cb0267e8bf72504a21c734e387f6e0797841ff5388dfff"
ENGINE_SHA256 = "525bdbddf2f180824944ee2576702e040c2f5554ca4290f40faec842e2cf02bb"
MOUSE_ACTIVE_RVA = 0x748E4
ADAPTERS = {
    "Initialize": "UHL_Initialize",
    "IN_Accumulate": "UHL_Accumulate",
    "CL_CreateMove": "UHL_CreateMove",
    "HUD_Frame": "UHL_Frame",
    "HUD_Shutdown": "UHL_Shutdown",
    "IN_ClearStates": "UHL_ClearStates",
    "HUD_Key_Event": "UHL_KeyEvent",
    "IN_MouseEvent": "UHL_MouseEvent",
    "HUD_Init": "UHL_HudInit",
}
PROFILE_FORMAT = "<8s14I"
PROFILE_SIZE = struct.calcsize(PROFILE_FORMAT)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def align(value: int, alignment: int) -> int:
    if alignment <= 0 or alignment & (alignment - 1):
        raise ValueError("Invalid PE alignment")
    return (value + alignment - 1) & ~(alignment - 1)


def build_client(data: bytes, mouse_active_rva: int) -> bytes:
    """Internal PE transformation. CLI/stager must check the source hash first."""
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
        raise ValueError("Expected a 32-bit x86 PE DLL")
    if any(s.Name.rstrip(b"\0") == b".uhl" for s in pe.sections):
        raise ValueError("Client is already patched")
    if pe.OPTIONAL_HEADER.DATA_DIRECTORY[4].VirtualAddress:
        raise ValueError("Refusing to invalidate a signed client")
    if pe.get_overlay():
        raise ValueError("Unexpected overlay; refusing to discard it")
    exports = {e.name.decode("ascii"): e for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    originals = [exports[name].address for name in ADAPTERS]
    if not 0 < mouse_active_rva < pe.OPTIONAL_HEADER.SizeOfImage - 4:
        raise ValueError("Invalid mouse-active RVA")
    user32 = next(i for i in pe.DIRECTORY_ENTRY_IMPORT if i.dll.lower() == b"user32.dll")
    iats = {i.name: i.address - pe.OPTIONAL_HEADER.ImageBase for i in user32.imports}
    get_iat, set_iat = iats[b"GetCursorPos"], iats[b"SetCursorPos"]

    header_offset = pe.sections[-1].get_file_offset() + 40
    first_raw = min(s.PointerToRawData for s in pe.sections if s.PointerToRawData)
    if header_offset + 80 > min(pe.OPTIONAL_HEADER.SizeOfHeaders, first_raw):
        raise ValueError("Insufficient room for two section headers")
    if any(data[header_offset:header_offset + 80]):
        raise ValueError("Section header space is not empty")
    section_rva = align(max(s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData)
                            for s in pe.sections), pe.OPTIONAL_HEADER.SectionAlignment)
    raw_offset = align(len(data), pe.OPTIONAL_HEADER.FileAlignment)
    payload = bytearray(struct.pack(PROFILE_FORMAT, b"UHLRAW3\0", 3, PROFILE_SIZE,
                                    *originals, mouse_active_rva, get_iat, set_iat))

    def allocate(content: bytes, alignment: int = 4) -> int:
        payload.extend(b"\0" * (align(len(payload), alignment) - len(payload)))
        rva = section_rva + len(payload)
        payload.extend(content)
        return rva

    # Preserve original descriptors and every existing IAT slot. Only the
    # directory's location changes; existing code/IAT addresses stay intact.
    descriptors = b"".join(i.struct.__pack__() for i in pe.DIRECTORY_ENTRY_IMPORT)
    import_rva = allocate(descriptors + b"\0" * 40)  # new descriptor and terminator
    dll_name = allocate(b"UHLCompat.dll\0", 2)
    hint_names = [allocate(b"\0\0" + name.encode("ascii") + b"\0", 2)
                  for name in ADAPTERS.values()]
    thunk_bytes = struct.pack("<" + "I" * (len(hint_names) + 1), *hint_names, 0)
    lookup_rva = allocate(thunk_bytes)
    iat_rva = allocate(thunk_bytes)
    descriptor_offset = import_rva - section_rva + len(descriptors)
    struct.pack_into("<5I", payload, descriptor_offset, lookup_rva, 0, 0, dll_name, iat_rva)

    relocations = bytearray()
    for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
        if block.struct.SizeOfBlock >= 8:
            start = block.struct.get_file_offset()
            relocations.extend(data[start:start + block.struct.SizeOfBlock])
    # The six-byte import jumps, spaced eight bytes apart, occupy one page.
    entry_count = len(ADAPTERS) + len(ADAPTERS) % 2
    added_reloc_size = 8 + 2 * entry_count
    reloc_rva = allocate(bytes(relocations) + b"\0" * added_reloc_size)
    code_rva = align(section_rva + len(payload), pe.OPTIONAL_HEADER.SectionAlignment)
    if code_rva & 0xFFF:
        raise ValueError("Expected page-aligned sections")
    stubs, code, entries = [], bytearray(), []
    for index in range(len(ADAPTERS)):
        # Ordinary x86 import jump, safe for detour libraries to relocate.
        # A call/pop PIC idiom would NOT be safe when BXT hooks HUD_Frame.
        stub_rva = code_rva + len(code)
        stubs.append(stub_rva)
        code.extend(b"\xff\x25" + struct.pack("<I",
                    pe.OPTIONAL_HEADER.ImageBase + iat_rva + index * 4) + b"\x90\x90")
        entries.append(0x3000 | ((stub_rva + 2) & 0xFFF))
    if len(entries) % 2:
        entries.append(0)  # IMAGE_REL_BASED_ABSOLUTE padding
    added_reloc = struct.pack("<II", code_rva, added_reloc_size) + struct.pack("<" + "H" * len(entries), *entries)
    offset = reloc_rva - section_rva + len(relocations)
    payload[offset:offset + added_reloc_size] = added_reloc

    output = bytearray(data)
    output.extend(b"\0" * (raw_offset - len(output)))
    raw_size = align(len(payload), pe.OPTIONAL_HEADER.FileAlignment)
    output.extend(payload)
    output.extend(b"\0" * (raw_size - len(payload)))
    code_raw_offset = len(output)
    code_raw_size = align(len(code), pe.OPTIONAL_HEADER.FileAlignment)
    output.extend(code)
    output.extend(b"\0" * (code_raw_size - len(code)))
    # Separate RW data/IAT and RX code: never create an RWX section.
    data_header = struct.pack("<8s6I2HI", b".uhl\0\0\0\0", len(payload), section_rva,
                              raw_size, raw_offset, 0, 0, 0, 0, 0xC0000040)
    code_header = struct.pack("<8s6I2HI", b".uhlc\0\0\0", len(code), code_rva,
                              code_raw_size, code_raw_offset, 0, 0, 0, 0, 0x60000020)
    output[header_offset:header_offset + 80] = data_header + code_header

    def set_field(obj, name: str, value: int, fmt: str = "<I") -> None:
        struct.pack_into(fmt, output, obj.get_field_absolute_offset(name), value)

    set_field(pe.FILE_HEADER, "NumberOfSections", pe.FILE_HEADER.NumberOfSections + 2, "<H")
    set_field(pe.OPTIONAL_HEADER, "SizeOfImage",
              align(code_rva + len(code), pe.OPTIONAL_HEADER.SectionAlignment))
    set_field(pe.OPTIONAL_HEADER, "SizeOfCode", pe.OPTIONAL_HEADER.SizeOfCode + code_raw_size)
    set_field(pe.OPTIONAL_HEADER, "SizeOfInitializedData",
              pe.OPTIONAL_HEADER.SizeOfInitializedData + raw_size)
    import_directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    set_field(import_directory, "VirtualAddress", import_rva)
    set_field(import_directory, "Size", len(descriptors) + 40)
    set_field(pe.OPTIONAL_HEADER.DATA_DIRECTORY[5], "VirtualAddress", reloc_rva)
    set_field(pe.OPTIONAL_HEADER.DATA_DIRECTORY[5], "Size", len(relocations) + added_reloc_size)
    # Keep the original IAT directory: the loader uses it to temporarily make
    # the original read-only IAT writable. The added IAT is in an RW section.
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[11]  # bound imports
    set_field(directory, "VirtualAddress", 0)
    set_field(directory, "Size", 0)
    for name, stub_rva in zip(ADAPTERS, stubs):
        struct.pack_into("<I", output,
                         pe.get_offset_from_rva(pe.DIRECTORY_ENTRY_EXPORT.struct.AddressOfFunctions) +
                         4 * (exports[name].ordinal - pe.DIRECTORY_ENTRY_EXPORT.struct.Base), stub_rva)
    set_field(pe.OPTIONAL_HEADER, "CheckSum", 0)
    check = pefile.PE(data=bytes(output))
    set_field(pe.OPTIONAL_HEADER, "CheckSum", check.generate_checksum())
    return bytes(output)


def patch_file(source: Path, destination: Path) -> dict:
    data = source.read_bytes()
    if sha256(data) != CLIENT_SHA256:
        raise ValueError("Unsupported client SHA-256. Use the original valve_WON/cl_dlls/client.dll; "
                         "do not bypass this check for another build.")
    if source.resolve() == destination.resolve() or destination.exists():
        raise ValueError("Destination must be a NEW file, not the original client")
    patched = build_client(data, MOUSE_ACTIVE_RVA)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as file:
        file.write(patched)
    return {"source_sha256": sha256(data), "patched_sha256": sha256(patched),
            "source_bytes": len(data), "patched_bytes": len(patched)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    print(json.dumps(patch_file(args.source, args.destination), indent=2))


if __name__ == "__main__":
    main()
