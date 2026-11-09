// Exercises the real client's IN_Accumulate + IN_MouseMove with fake cursor and
// engine callbacks. No game, desktop input, pointer warp or Windows preference
// change. The licensed client is supplied locally, never part of the test repo.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include "../native/precision_mouse.hpp"

static void Check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
static void Near(float actual, float expected, const char* label) {
    if (std::fabs(actual - expected) > 0.00002f) {
        std::cerr << "actual=" << actual << " expected=" << expected << '\n'; Check(false, label);
    }
}
struct Cvar { const char* name = "test"; const char* text = "0"; int flags = 0; float value = 0; };
static BYTE* base;
template<class T> static T& At(DWORD rva) { return *reinterpret_cast<T*>(base + rva); }
static POINT cursor{};
static float angles[3]{};
static int __cdecl Center() { return 0; }
static void __cdecl GetAngles(float* out) { std::memcpy(out, angles, sizeof(angles)); }
static void __cdecl SetAngles(float* in) { std::memcpy(angles, in, sizeof(angles)); }
static double __cdecl ClientTime() { return 0; }
static BOOL WINAPI FakeGetCursor(LPPOINT out) { *out = cursor; return TRUE; }
static BOOL WINAPI FakeSetCursor(int, int) { return TRUE; }
static int __cdecl Noclip() { return 0; }
static void SetImport(DWORD rva, void* fn) {
    DWORD old = 0, ignored = 0;
    Check(VirtualProtect(base + rva, 4, PAGE_READWRITE, &old) != 0, "unprotect fake cursor import");
    At<void*>(rva) = fn;
    VirtualProtect(base + rva, 4, old, &ignored);
}
int main(int argc, char** argv) {
    if (argc < 2 || GetFileAttributesA(argv[1]) == INVALID_FILE_ATTRIBUTES) {
        std::cout << "SKIP: supported stock client.dll not found\n"; return 77;
    }
    std::string folder = argv[1];
    for (int i = 0; i < 3; ++i) folder = folder.substr(0, folder.find_last_of("\\/"));
    SetDllDirectoryA(folder.c_str());
    // Force relocation, exercising normalized instruction validation too.
    void* reserved = VirtualAlloc(reinterpret_cast<void*>(0x27000000), 0x100000, MEM_RESERVE, PAGE_NOACCESS);
    HMODULE module = LoadLibraryA(argv[1]); Check(module != nullptr, "load real stock client");
    base = reinterpret_cast<BYTE*>(module);
    std::array<BYTE, 0x1413d - 0x13f76> original{};
    std::memcpy(original.data(), base + 0x13f76, original.size());
    Check(PrecisionMouse::Install(module), "validate and install precision patch");
    Check(PrecisionMouse::Installed(), "precision status");
    Check(PrecisionMouse::Install(module), "idempotent install");

    At<void*>(0x670f8) = reinterpret_cast<void*>(Center);
    At<void*>(0x670fc) = reinterpret_cast<void*>(Center);
    At<void*>(0x67100) = reinterpret_cast<void*>(GetAngles);
    At<void*>(0x67104) = reinterpret_cast<void*>(SetAngles);
    At<void*>(0x67150) = reinterpret_cast<void*>(ClientTime);
    At<void*>(0x67140) = reinterpret_cast<void*>(Noclip);
    SetImport(0x3e128, reinterpret_cast<void*>(FakeGetCursor));
    SetImport(0x3e130, reinterpret_cast<void*>(FakeSetCursor));
    Cvar filter, sensitivity, yaw, pitch, lookstrafe, side, forward, up, pitchDown, pitchUp;
    yaw.value = pitch.value = 0.022f;
    side.value = forward.value = up.value = 1;
    pitchDown.value = 89; pitchUp.value = 89;
    At<Cvar*>(0x74838) = &filter; At<Cvar*>(0x748ec) = &sensitivity;
    At<Cvar*>(0x746fc) = &yaw; At<Cvar*>(0x74620) = &pitch;
    At<Cvar*>(0x74698) = &lookstrafe; At<Cvar*>(0x746cc) = &side;
    At<Cvar*>(0x7471c) = &forward; At<Cvar*>(0x74664) = &pitchDown; At<Cvar*>(0x746ac) = &pitchUp;
    At<int>(0x745ec) = At<int>(0x748f8) = At<int>(0x6706c) = 0;
    At<int>(0x748e4) = 1;
    auto mouseMove = reinterpret_cast<void (__cdecl*)(float, void*)>(base + 0x13ee0);
    auto accumulate = reinterpret_cast<void (__cdecl*)()>(GetProcAddress(module, "IN_Accumulate"));
    std::array<BYTE, 64> cmd{};
    auto reset = [&] {
        std::memset(angles, 0, sizeof(angles)); cmd.fill(0);
        At<int>(0x748f0) = At<int>(0x74828) = At<int>(0x74864) = At<int>(0x74860) = 0;
        At<int>(0x74748) = 0; At<int>(0x746a8) = 1; // mlook, no strafe
        At<float>(0x66738) = 0; filter.value = 0;
    };
    for (float value : {0.01f, 0.1f, 0.5f, 0.99f, 1.f, 3.f}) {
        for (int sign : {-1, 1}) {
            reset(); sensitivity.value = value;
            for (int n = 0; n < 100; ++n) { cursor = {sign, -sign}; mouseMove(0.01f, cmd.data()); }
            Near(angles[1], -100 * sign * value * yaw.value, "100 single counts retain fractional yaw");
            Near(angles[0], -100 * sign * value * pitch.value, "100 single counts retain fractional pitch");
        }
    }
    reset(); sensitivity.value = 0.1f;
    cursor = {1, -1}; mouseMove(0.01f, cmd.data());
    Near(angles[1], -0.0022f, "first count responds immediately; no integer remainder delay");
    Near(angles[0], -0.0022f, "vertical first count");
    cursor = {-1, 1}; mouseMove(0.01f, cmd.data());
    Near(angles[0], 0, "opposite pitch counts cancel"); Near(angles[1], 0, "opposite yaw counts cancel");
    reset(); filter.value = 1; cursor = {1, -1}; mouseMove(0.01f, cmd.data());
    Near(angles[1], -0.0011f, "filtered half-count retained");
    cursor = {0, 0}; mouseMove(0.01f, cmd.data());
    Near(angles[1], -0.0022f, "filter tail retained");
    reset(); At<float>(0x66738) = 0.025f; cursor = {1, 1}; mouseMove(0.01f, cmd.data());
    Near(angles[1], -0.025f * yaw.value, "HUD/zoom sensitivity override");
    reset(); sensitivity.value = 0; cursor = {100, 100}; mouseMove(0.01f, cmd.data());
    Near(angles[0], 0, "zero sensitivity stays zero"); Near(angles[1], 0, "zero yaw");
    reset(); sensitivity.value = 0.1f;
    cursor = {1, -1}; accumulate(); cursor = {2, -2}; accumulate();
    cursor = {0, 0}; mouseMove(0.25f, cmd.data());
    Near(angles[1], -0.3f * yaw.value, "low-FPS accumulated counts are consumed once");
    mouseMove(0.25f, cmd.data()); Near(angles[1], -0.3f * yaw.value, "no duplicate accumulated input");
    reset(); At<int>(0x74748) = 1; cursor = {1, 1}; mouseMove(0.01f, cmd.data());
    float sidemove = 0; std::memcpy(&sidemove, cmd.data() + 0x14, 4);
    Near(sidemove, 0.1f, "stock mouse strafe uses fractional scaled X");
    reset(); sensitivity.value = 10000; cursor = {0, 100}; mouseMove(0.01f, cmd.data());
    Near(angles[0], 89, "stock downward pitch clamp");
    cursor = {0, -100}; mouseMove(0.01f, cmd.data()); Near(angles[0], -89, "stock upward pitch clamp");

    PrecisionMouse::Shutdown(); Check(!PrecisionMouse::Installed(), "shutdown status");
    Check(!std::memcmp(original.data(), base + 0x13f76, original.size()), "all original instructions restored");
    // Control: the same one-count input is actually lost by the unpatched client.
    reset(); sensitivity.value = 0.1f; cursor = {1, -1}; mouseMove(0.01f, cmd.data());
    Near(angles[0], 0, "stock control drops sub-one pitch"); Near(angles[1], 0, "stock control drops sub-one yaw");
    DWORD old = 0, ignored = 0;
    VirtualProtect(base + 0x14037, 1, PAGE_EXECUTE_READWRITE, &old);
    const BYTE saved = base[0x14037]; base[0x14037] = 0x90;
    Check(!PrecisionMouse::Install(module), "reject a modified instruction block");
    Check(base[0x13f76] == original[0], "rejected patch leaves entry untouched");
    base[0x14037] = saved; VirtualProtect(base + 0x14037, 1, old, &ignored);
    FreeLibrary(module);
    if (reserved) VirtualFree(reserved, 0, MEM_RELEASE);
    std::cout << "Real-client precision mouse tests passed (fractional X/Y, filter, zoom, zero, accumulate, strafe, pitch, rollback, stock control).\n";
}
