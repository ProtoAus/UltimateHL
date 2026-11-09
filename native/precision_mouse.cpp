#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include "precision_mouse.hpp"

namespace PrecisionMouse {
namespace {
BYTE* client = nullptr;
constexpr DWORD begin = 0x13f76, resume = 0x1404f, end = 0x1413d;
constexpr DWORD consumers[] = {0x14081, 0x1409a, 0x140c2, 0x14120, 0x14137};
struct Change {
    DWORD rva;
    unsigned size;
    std::array<BYTE, 16> before{}, after{};
};
std::array<Change, 7> changes{};

template<class T> T& At(DWORD rva) { return *reinterpret_cast<T*>(client + rva); }
struct Cvar { const char* name; const char* text; int flags; float value; };

void __cdecl ScaleMouse(int x, int y) {
    // Only the two integer truncations (filter and sensitivity) are removed.
    // The original IN_MouseMove still handles m_yaw/m_pitch, strafing, movement,
    // pitch clamps, view angles and cursor recentering after this calculation.
    const bool filter = At<Cvar*>(0x74838)->value != 0;
    const float dx = filter ? (float(x) + float(At<int>(0x74864))) * 0.5f : float(x);
    const float dy = filter ? (float(y) + float(At<int>(0x74860))) * 0.5f : float(y);
    At<int>(0x74864) = x;
    At<int>(0x74860) = y;
    // CHud::GetSensitivity, including the stock zoom override. Keep the call
    // rather than caching sensitivity: console/zoom changes apply immediately.
    using GetSensitivity = float (__thiscall*)(void*);
    const float overrideValue = reinterpret_cast<GetSensitivity>(client + 0xe8c0)(client + 0x66720);
    const float sensitivity = overrideValue != 0 ? overrideValue : At<Cvar*>(0x748ec)->value;
    At<float>(0x7481c) = dx * sensitivity;
    At<float>(0x74814) = dy * sensitivity;
}

// Hash the complete bypassed instruction block after undoing PE base relocations.
// This also supports the old .uhl client layout, whose original code is intact.
// It refuses other clients or another tool's edits instead of guessing offsets.
bool MatchesPipeline(BYTE* base, const IMAGE_NT_HEADERS& nt) {
    std::array<BYTE, resume - begin> bytes{};
    std::memcpy(bytes.data(), base + begin, bytes.size());
    const auto& dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const DWORD imageSize = nt.OptionalHeader.SizeOfImage;
    if (!dir.VirtualAddress || dir.VirtualAddress >= imageSize || dir.Size > imageSize - dir.VirtualAddress) return false;
    DWORD offset = 0;
    while (offset < dir.Size) {
        if (dir.Size - offset < sizeof(IMAGE_BASE_RELOCATION)) return false;
        const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base + dir.VirtualAddress + offset);
        if (block->SizeOfBlock < sizeof(*block) || block->SizeOfBlock > dir.Size - offset || (block->SizeOfBlock & 1)) return false;
        const auto* entries = reinterpret_cast<const WORD*>(block + 1);
        const DWORD count = (block->SizeOfBlock - sizeof(*block)) / sizeof(WORD);
        for (DWORD i = 0; i < count; ++i) {
            const DWORD rva = block->VirtualAddress + (entries[i] & 0xfff);
            if ((entries[i] >> 12) != IMAGE_REL_BASED_HIGHLOW || rva < begin || rva >= resume) continue;
            if (rva > resume - 4) return false;
            DWORD value = 0;
            std::memcpy(&value, bytes.data() + rva - begin, 4);
            value -= static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(base)) - 0x27000000u;
            std::memcpy(bytes.data() + rva - begin, &value, 4);
        }
        offset += block->SizeOfBlock;
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (BYTE byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
    return hash == 0x49e8fbe4639c7701ull;
}
void Relative(BYTE* instruction, BYTE opcode, const void* target, const BYTE* address) {
    instruction[0] = opcode;
    const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target) -
                                                reinterpret_cast<std::uintptr_t>(address + 5));
    std::memcpy(instruction + 1, &delta, 4);
}
bool WriteChanges(bool install) {
    for (const auto& change : changes) {
        const auto& expected = install ? change.before : change.after;
        if (std::memcmp(client + change.rva, expected.data(), change.size)) return false;
    }
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(client + begin, end - begin, PAGE_EXECUTE_READWRITE, &old)) return false;
    for (const auto& change : changes) {
        const auto& value = install ? change.after : change.before;
        std::memcpy(client + change.rva, value.data(), change.size);
    }
    VirtualProtect(client + begin, end - begin, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), client + begin, end - begin);
    return true;
}
} // namespace

bool Install(void* module) {
    if (client) return client == module;
    if (!module) return false;
    auto* base = static_cast<BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.SizeOfImage < 0x748f0 || !MatchesPipeline(base, *nt)) return false;

    // push y; push x; call ScaleMouse; add esp,8; jmp original look logic.
    // Normal x86 cdecl preserves ESI/EDI/EBX; the following stock instructions
    // overwrite volatile registers and flags before using them. No naked asm.
    changes[0] = {begin, 16};
    auto& entry = changes[0].after;
    entry.fill(0x90);
    entry[0] = 0x57; entry[1] = 0x56;
    Relative(entry.data() + 2, 0xe8, reinterpret_cast<const void*>(ScaleMouse), base + begin + 2);
    entry[7] = 0x83; entry[8] = 0xc4; entry[9] = 8;
    Relative(entry.data() + 10, 0xe9, base + resume, base + begin + 10);
    // EAX used to contain integer Y. ScaleMouse already wrote float Y.
    changes[1] = {0x1405c, 5};
    changes[1].after.fill(0x90);
    const DWORD yAddress = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(base + 0x74814));
    if (base[0x1405c] != 0xa3 || std::memcmp(base + 0x1405d, &yAddress, 4)) return false;
    for (unsigned i = 0; i < 5; ++i) {
        const DWORD rva = consumers[i];
        const DWORD address = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(base + (i < 2 ? 0x7481c : 0x74814)));
        if (base[rva] != 0xdb || base[rva + 1] != 5 || std::memcmp(base + rva + 2, &address, 4)) return false;
        changes[i + 2] = {rva, 6};
        std::memcpy(changes[i + 2].after.data(), base + rva, 6);
        changes[i + 2].after[0] = 0xd9; // FILD int32 -> FLD float32, same instruction length
    }
    for (auto& change : changes) std::memcpy(change.before.data(), base + change.rva, change.size);
    client = base;
    if (!WriteChanges(true)) { client = nullptr; return false; }
    return true;
}
bool Installed() { return client != nullptr; }
void Shutdown() {
    if (!client || !WriteChanges(false)) return;
    client = nullptr;
}
} // namespace PrecisionMouse
