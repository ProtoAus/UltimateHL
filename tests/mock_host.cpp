#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <fstream>

struct Cvar { char* name; char* string; int flags; float value; Cvar* next; };
static Cvar raw{};
static bool statusAdded = false;
static Cvar* __cdecl Register(char* name, char* value, int flags) {
    raw = {name, value, flags, 0, nullptr}; // headless: use stock input
    return &raw;
}
static int __cdecl AddCommand(char*, void (__cdecl*)()) { statusAdded = true; return 1; }
static int __cdecl Center() { return 320; }
static BOOL WINAPI FakeCursor(POINT* point) { *point = {1234, 5678}; return TRUE; }
static int mouseWrites = 0, mouseReads = 0, otherSettings = 0;
static BOOL WINAPI FakeSystemParameters(UINT action, UINT, PVOID value, UINT) {
    if (action == SPI_SETMOUSE || action == SPI_SETMOUSESPEED) ++mouseWrites;
    else if (action == SPI_GETMOUSE) {
        ++mouseReads;
        auto* values = static_cast<int*>(value);
        values[0] = 6; values[1] = 10; values[2] = 1;
    } else ++otherSettings;
    return TRUE; // Backstop: this fake NEVER changes real Windows preferences.
}
static bool SetMockMouseSettingsImport(HMODULE module) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; entry->Name; ++entry) {
        if (!entry->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + entry->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(name->Name), "SystemParametersInfoA")) continue;
            DWORD old = 0, ignored = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(FakeSystemParameters);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &ignored);
            return true;
        }
    }
    return false;
}
static void SetMockCursorImport(HMODULE module) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".uhl", 4)) continue;
        const auto rva = *reinterpret_cast<DWORD*>(base + section->VirtualAddress + 56);
        auto** slot = reinterpret_cast<void**>(base + rva);
        DWORD old, ignored;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) std::exit(1);
        *slot = reinterpret_cast<void*>(FakeCursor);
        VirtualProtect(slot, sizeof(*slot), old, &ignored);
        return;
    }
    std::exit(1);
}
static void __cdecl Print(const char* text) { std::cout << text; }
static void Check(bool condition, const char* text) {
    if (!condition) { std::cerr << "FAIL: " << text << " (Win32 " << GetLastError() << ")\n"; std::exit(1); }
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG {
        MEMORY_BASIC_INFORMATION info{};
        auto address = exception->ExceptionRecord->ExceptionAddress;
        VirtualQuery(address, &info, sizeof(info));
        wchar_t name[MAX_PATH]{};
        GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), name, MAX_PATH);
        std::wcerr << L"Crash in " << name << L" at RVA " << std::hex
                   << (reinterpret_cast<std::uintptr_t>(address) -
                       reinterpret_cast<std::uintptr_t>(info.AllocationBase)) << L'\n';
        return EXCEPTION_EXECUTE_HANDLER;
    });
    Check(argc == 2, "patched mock-client path argument");
    int realMouseBefore[3]{}, realSpeedBefore = 0;
    Check(SystemParametersInfoA(SPI_GETMOUSE, 0, realMouseBefore, 0) &&
          SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &realSpeedBefore, 0), "read Windows mouse preferences (no changes)");
    Check(IsProcessDPIAware() != FALSE, "external manifest establishes DPI awareness BEFORE loading the client");
    std::cout << "Early DPI-aware startup: screen " << GetSystemMetrics(SM_CXSCREEN) << 'x'
              << GetSystemMetrics(SM_CYSCREEN) << " (physical desktop coordinates).\n";
    auto path = std::filesystem::absolute(argv[1]);
    // Force the client off its preferred image base to exercise the added
    // HIGHLOW relocations, rather than accidentally passing at one address.
    char headers[4096]{};
    std::ifstream file(path, std::ios::binary);
    file.read(headers, sizeof(headers));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(headers);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(headers + dos->e_lfanew);
    void* reservation = VirtualAlloc(reinterpret_cast<void*>(nt->OptionalHeader.ImageBase),
                                    nt->OptionalHeader.SizeOfImage, MEM_RESERVE, PAGE_NOACCESS);
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Check(module != nullptr, "Windows loader accepts appended imports and relocated export stubs");
    if (reservation) Check(reinterpret_cast<void*>(module) != reservation, "client is relocated");
    auto get = [module](const char* name) {
        auto result = GetProcAddress(module, name);
        Check(result != nullptr, name);
        return result;
    };
    void* funcs[87]{};
    funcs[14] = reinterpret_cast<void*>(Register);
    funcs[17] = reinterpret_cast<void*>(AddCommand);
    funcs[30] = reinterpret_cast<void*>(Print);
    funcs[32] = funcs[33] = reinterpret_cast<void*>(Center);
    std::cout << "Mock module loaded; setting sentinel IAT.\n";
    SetMockCursorImport(module);
    Check(SetMockMouseSettingsImport(module), "install fake SPI backstop BEFORE any mouse-write probes");
    *reinterpret_cast<int*>(get("mockMousePolicyProbeEnabled")) = 1;
    auto initialize = reinterpret_cast<int (__cdecl*)(void*, int)>(get("Initialize"));
    std::cout << "Calling adapted Initialize.\n";
    Check(initialize(funcs, 7) == 1, "Initialize forwards and preserves return value");
    Check(raw.name && statusAdded, "native module registers cvar and diagnostic command");
    auto calls = reinterpret_cast<int*>(get("mockCalls"));
    Check(*calls == 1, "original Initialize called once");
    Check(mouseWrites == 0, "mouse-settings writes blocked even inside original Initialize");
    auto tryMouseWrites = reinterpret_cast<BOOL (__cdecl*)()>(get("MockTryMouseWrites"));
    Check(tryMouseWrites() && mouseWrites == 0, "EPP and pointer-speed writes report success without reaching OS/backstop");
    int queried[3]{};
    Check(reinterpret_cast<BOOL (__cdecl*)(int*)>(get("MockReadMouse"))(queried) && mouseReads == 1 && queried[0] == 6 && queried[1] == 10 && queried[2] == 1,
          "SPI_GETMOUSE still passes through unchanged");
    Check(reinterpret_cast<BOOL (__cdecl*)()>(get("MockOtherSetting"))() && otherSettings == 1,
          "unrelated settings pass to the fake (never to real Windows in this test)");
    reinterpret_cast<void (__cdecl*)()>(get("HUD_Init"))();
    POINT after{};
    auto readCursor = reinterpret_cast<BOOL (__cdecl*)(POINT*)>(get("MockReadCursor"));
    Check(readCursor(&after) != FALSE, "out-of-scope client cursor API succeeds");
    Check(after.x == 1234 && after.y == 5678,
          "UI cursor path is unchanged by raw adapter");
    reinterpret_cast<void (__cdecl*)()>(get("IN_Accumulate"))();
    reinterpret_cast<void (__cdecl*)(float, void*, int)>(get("CL_CreateMove"))(0.01f, nullptr, 1);
    reinterpret_cast<void (__cdecl*)(double)>(get("HUD_Frame"))(1.0);
    reinterpret_cast<void (__cdecl*)()>(get("IN_ClearStates"))();
    Check(reinterpret_cast<int (__cdecl*)(int, int, const char*)>(get("HUD_Key_Event"))(1, 119, "+forward") == 42,
          "key diagnostic adapter preserves original result");
    reinterpret_cast<void (__cdecl*)(int)>(get("IN_MouseEvent"))(3);
    reinterpret_cast<void (__cdecl*)()>(get("HUD_Shutdown"))();
    Check(*calls == 9, "all nine export adapters forward exactly once");
    Check(tryMouseWrites() && mouseWrites == 0, "cached mouse-settings restore is blocked after HUD_Shutdown too");
    Check(readCursor(&after) != FALSE && after.x == 1234 && after.y == 5678,
          "prior API hook is restored after shutdown");
    Check(FreeLibrary(module) != FALSE, "mock client unloads cleanly");
    if (reservation) VirtualFree(reservation, 0, MEM_RELEASE);
    int realMouseAfter[3]{}, realSpeedAfter = 0;
    Check(SystemParametersInfoA(SPI_GETMOUSE, 0, realMouseAfter, 0) &&
          SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &realSpeedAfter, 0), "read Windows mouse preferences after test");
    Check(!std::memcmp(realMouseBefore, realMouseAfter, sizeof(realMouseBefore)) && realSpeedBefore == realSpeedAfter,
          "real Windows acceleration and pointer speed stayed unchanged; no restore was attempted");
    std::cout << "Windows mouse-settings guard passed: initialization/shutdown writes blocked, reads preserved, real preferences unchanged.\n";
    std::cout << "Native loader/adapter smoke test passed (headless; NOT an in-game raw-input test).\n";
}
