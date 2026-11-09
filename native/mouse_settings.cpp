#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "mouse_settings.hpp"

namespace {
using SpiFn = BOOL (WINAPI*)(UINT, UINT, PVOID, UINT);
struct Import { void** slot = nullptr; SpiFn original = nullptr; };
Import ansi, wide;
volatile LONG blocked = 0;

BOOL Dispatch(const Import& entry, UINT action, UINT parameter, PVOID value, UINT flags) {
    // SPI_SETMOUSE changes the acceleration/EPP thresholds and enable state.
    // SPI_SETMOUSESPEED changes the Windows pointer-speed slider. Neither the
    // game entering play nor its cached-value restore is allowed to write them.
    if (action == SPI_SETMOUSE || action == SPI_SETMOUSESPEED) {
        InterlockedIncrement(&blocked);
        return TRUE; // No OS call, registry write, broadcast, or restore later.
    }
    return entry.original(action, parameter, value, flags);
}
BOOL WINAPI GuardA(UINT action, UINT parameter, PVOID value, UINT flags) {
    return Dispatch(ansi, action, parameter, value, flags);
}
BOOL WINAPI GuardW(UINT action, UINT parameter, PVOID value, UINT flags) {
    return Dispatch(wide, action, parameter, value, flags);
}

void** FindImport(BYTE* base, const char* api) {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return nullptr;
    const auto size = nt->OptionalHeader.SizeOfImage;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto within = [size](DWORD rva, std::size_t length) { return rva && rva < size && length <= size - rva; };
    if (!within(directory.VirtualAddress, directory.Size)) return nullptr;
    const auto* entries = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (unsigned i = 0; i < directory.Size / sizeof(*entries); ++i) {
        const auto& entry = entries[i];
        if (!entry.Name) break;
        if (!within(entry.Name, sizeof("USER32.dll")) || _strnicmp(reinterpret_cast<char*>(base + entry.Name), "USER32.dll", sizeof("USER32.dll"))) continue;
        if (!entry.OriginalFirstThunk || !entry.FirstThunk) continue;
        for (DWORD index = 0; index < size / sizeof(IMAGE_THUNK_DATA32); ++index) {
            const DWORD names = entry.OriginalFirstThunk + index * sizeof(IMAGE_THUNK_DATA32);
            const DWORD slots = entry.FirstThunk + index * sizeof(IMAGE_THUNK_DATA32);
            if (!within(names, sizeof(IMAGE_THUNK_DATA32)) || !within(slots, sizeof(void*))) break;
            const auto thunk = *reinterpret_cast<IMAGE_THUNK_DATA32*>(base + names);
            if (!thunk.u1.AddressOfData) break;
            if (IMAGE_SNAP_BY_ORDINAL32(thunk.u1.Ordinal)) continue;
            const auto name = thunk.u1.AddressOfData + offsetof(IMAGE_IMPORT_BY_NAME, Name);
            if (!within(name, std::strlen(api) + 1)) continue;
            if (!std::memcmp(base + name, api, std::strlen(api) + 1)) return reinterpret_cast<void**>(base + slots);
        }
    }
    return nullptr;
}
bool Hook(void** slot, Import& entry, SpiFn guard) {
    if (!slot) return true; // A client may import only the ANSI version.
    if (entry.slot == slot && *slot == reinterpret_cast<void*>(guard)) return true;
    void* expected = *slot;
    if (!expected) return false;
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    entry = {slot, reinterpret_cast<SpiFn>(expected)};
    const bool replaced = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(guard), expected) == expected;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return replaced;
}
}

namespace MouseSettings {
bool Install(void* clientModule) {
    auto* base = static_cast<BYTE*>(clientModule);
    if (!base) return false;
    auto** a = FindImport(base, "SystemParametersInfoA");
    auto** w = FindImport(base, "SystemParametersInfoW");
    return (a || w) && Hook(a, ansi, GuardA) && Hook(w, wide, GuardW) && Protected();
}
bool Protected() {
    return (ansi.slot || wide.slot) &&
        (!ansi.slot || *ansi.slot == reinterpret_cast<void*>(GuardA)) &&
        (!wide.slot || *wide.slot == reinterpret_cast<void*>(GuardW));
}
unsigned BlockedWrites() { return static_cast<unsigned>(InterlockedCompareExchange(&blocked, 0, 0)); }
void Status(void (*print)(const char*)) {
    print(Protected() ? "Windows mouse settings: write-protected (EPP/acceleration and pointer speed; no cached restore).\n" :
                        "Windows mouse settings: protection unavailable.\n");
    char message[96]{};
    sprintf_s(message, "Blocked client mouse-settings writes: %u (reads/unrelated settings pass through).\n", BlockedWrites());
    print(message);
}
}
