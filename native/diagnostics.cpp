#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <initializer_list>
#include <cstdint>
#include <fcntl.h>
#include <io.h>
#include "diagnostics.hpp"
#include "engine_bridge.hpp"
#include "mouse_settings.hpp"
#include "presentation.hpp"

namespace {
void* api[87]{};
FILE* trace = nullptr;
LARGE_INTEGER traceStart{}, frequency{};
unsigned long long sequence = 0;
char tracePath[MAX_PATH]{};
using PrintFn = void (__cdecl*)(const char*);
using CvarFn = UhlCvar* (__cdecl*)(const char*);
template<class T> T Function(unsigned i) { return reinterpret_cast<T>(api[i]); }
void Print(const char* text) { if (api[30]) Function<PrintFn>(30)(text); }
void Line(FILE* file, const char* format, ...) {
    char text[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    if (file) fputs(text, file);
    Print(text);
}
FILE* NewFile(const char* label, const char* extension, char* path) {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char name[160]{};
    sprintf_s(name, "uhl-%s-%04u%02u%02u-%02u%02u%02u-%03u-%lu.%s", label,
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond, now.wMilliseconds, GetCurrentProcessId(), extension);
    if (!GetFullPathNameA(name, MAX_PATH, path, nullptr)) return nullptr;
    // A collision must not overwrite a previous capture. Keep the SAME handle
    // when wrapping it in stdio: no reopen/truncation race, and live read-only
    // observers may see flushed load markers without affecting game input.
    HANDLE handle = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    const int descriptor=_open_osfhandle(reinterpret_cast<std::intptr_t>(handle),_O_WRONLY|_O_TEXT);
    if (descriptor<0) { CloseHandle(handle);return nullptr; }
    FILE* result=_fdopen(descriptor,"w");
    if (!result) _close(descriptor); // Also closes the transferred OS handle.
    return result;
}
void CvarLine(FILE* file, const char* name) {
    UhlCvar* cvar = api[72] ? Function<CvarFn>(72)(name) : nullptr;
    if (cvar) Line(file, "%s = %s (%.9g)\n", name, cvar->string ? cvar->string : "<null>", cvar->value);
    else Line(file, "%s = <not available>\n", name);
}
}

namespace Diagnostics {
bool Readable(const void* address, unsigned bytes) {
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (!start || start + bytes < start) return false;
    auto current = start;
    while (current < start + bytes) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (end <= current) return false;
        current = end;
    }
    return true;
}
void Initialize(void* functions) {
    std::memcpy(api, functions, sizeof(api));
    QueryPerformanceFrequency(&frequency);
    EnsureCommands();
}
void EnsureCommands() {
    EngineBridge::EnsureCommand("uhl_diagnostics", Report);
    EngineBridge::EnsureCommand("uhl_trace_start", StartTrace);
    EngineBridge::EnsureCommand("uhl_trace_stop", StopTrace);
}
void Report() {
    char path[MAX_PATH]{};
    FILE* file = NewFile("diagnostics", "txt", path);
    if (!file) { Print("UltimateHL: could not create diagnostic report in the game's working directory.\n"); return; }
    Line(file, "UltimateHL v3 diagnostics; read-only snapshot, not a verification of fixes.\n");
    Line(file, "Process DPI aware: %d (0 = DPI virtualization remains enabled).\n", IsProcessDPIAware());
    Line(file, "Windows mouse-settings protection: %s; blocked writes=%u (no restore)\n",
         MouseSettings::Protected() ? "active" : "NOT active", MouseSettings::BlockedWrites());
    HWND window = static_cast<HWND>(Presentation::GameWindow());
    RECT outer{}, inner{};
    POINT origin{}, cursor{}, physical{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &inner);
    ClientToScreen(window, &origin);
    GetCursorPos(&cursor);
    GetPhysicalCursorPos(&physical);
    Line(file, "window=%p rect=(%ld,%ld)-(%ld,%ld) client=%ldx%ld client_origin=(%ld,%ld)\n",
         window, outer.left, outer.top, outer.right, outer.bottom,
         inner.right - inner.left, inner.bottom - inner.top, origin.x, origin.y);
    Line(file, "desktop cursor=(%ld,%ld), physical cursor=(%ld,%ld), screen=%dx%d\n",
         cursor.x, cursor.y, physical.x, physical.y, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (api[32] && api[33])
        Line(file, "engine center=(%d,%d)\n", Function<int (__cdecl*)()>(32)(), Function<int (__cdecl*)()>(33)());
    if (api[49]) {
        int x = 0, y = 0;
        Function<void (__cdecl*)(int*, int*)>(49)(&x, &y);
        Line(file, "engine GetMousePosition=(%d,%d) [engine-local coordinates, not necessarily screen coordinates]\n", x, y);
    }
    struct ScreenInfo { int size, width, height, flags, charHeight; short widths[256]; };
    if (api[12]) {
        ScreenInfo screen{};
        screen.size = sizeof(screen);
        if (Function<int (__cdecl*)(ScreenInfo*)>(12)(&screen))
            Line(file, "client render size=%dx%d flags=%d char_height=%d\n", screen.width, screen.height, screen.flags, screen.charHeight);
    }
    Line(file, "window styles=0x%08lx exstyles=0x%08lx\n", static_cast<unsigned long>(GetWindowLongW(window, GWL_STYLE)), static_cast<unsigned long>(GetWindowLongW(window, GWL_EXSTYLE)));
    DWORD affinity = 0;
    if (GetWindowDisplayAffinity(window, &affinity)) Line(file, "capture display affinity=%lu (0=not excluded)\n", affinity);
    const char* names[] = {"uhl_rawinput", "uhl_load_fps_min", "uhl_preserve_load_keys", "uhl_borderless", "fps_max", "fps_modem", "clockwindow", "gl_spriteblend",
        "host_framerate", "vgui_emulatemouse", "hud_capturemouse", "viewsize", "gl_round_down",
        "gl_picmip", "gl_max_size", "m_filter", "sensitivity", "pausable", "uhl_native_textures", "uhl_hud_texels"};
    for (const auto* name : names) CvarLine(file, name);
    if (api[74]) Line(file, "level=%s\n", Function<const char* (__cdecl*)()>(74)());
    if (api[71]) Line(file, "game_directory=%s\n", Function<const char* (__cdecl*)()>(71)());
    Line(file, "Key_Event API entry=%p; AddCommand API entry=%p\n", api[48], api[17]);
    for (const auto* name : {"uhl_help", "uhl_status", "uhl_diagnostics", "uhl_trace_start", "uhl_trace_stop", "uhl_fps"})
        Line(file, "command %s: %s\n", name, EngineBridge::HasCommand(name) ? "registered" : "not verified");
    Line(file, "gl_texturemode is an engine command, not a cvar; query it directly in the console.\n");
    if (HMODULE gl = GetModuleHandleW(L"opengl32.dll")) {
        auto get = reinterpret_cast<void (WINAPI*)(unsigned, int*)>(GetProcAddress(gl, "glGetIntegerv"));
        if (get) {
            int viewport[4]{}, maximum = 0;
            get(0x0ba2, viewport); // GL_VIEWPORT
            get(0x0d33, &maximum); // GL_MAX_TEXTURE_SIZE: not a safe engine-buffer limit.
            Line(file, "GL viewport=(%d,%d,%d,%d), hardware max texture=%d\n",
                 viewport[0], viewport[1], viewport[2], viewport[3], maximum);
        }
    }
    fclose(file);
    Print("UltimateHL diagnostic report: "); Print(path); Print("\n");
}
void StartTrace() {
    if (trace) { Print("UltimateHL: input trace is already running.\n"); return; }
    trace = NewFile("input", "tsv", tracePath);
    if (!trace) { Print("UltimateHL: could not create input trace.\n"); return; }
    QueryPerformanceCounter(&traceStart);
    sequence = 0;
    fputs("sequence\tseconds\tevent\ta\tb\tc\n", trace);
    Event("trace_start");
    Print("UltimateHL: local key-code/input trace started: "); Print(tracePath);
    Print("\nUse uhl_trace_stop after the test. Tracing can change timings; do not benchmark or submit runs with tracing.\n");
}
void StopTrace() {
    if (!trace) { Print("UltimateHL: no input trace is running.\n"); return; }
    Event("trace_stop");
    fclose(trace);
    trace = nullptr;
    Print("UltimateHL: input trace saved: "); Print(tracePath); Print("\n");
}
void Event(const char* kind, std::int64_t a, std::int64_t b, std::int64_t c) {
    if (!trace) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double seconds = static_cast<double>(now.QuadPart - traceStart.QuadPart) / static_cast<double>(frequency.QuadPart);
    fprintf(trace, "%llu\t%.9f\t%s\t%lld\t%lld\t%lld\n", ++sequence, seconds, kind,
            static_cast<long long>(a), static_cast<long long>(b), static_cast<long long>(c));
    if (!std::strncmp(kind, "load_", 5)) fflush(trace);
}
void UserCommand(const void* pointer, bool active) {
    if (!trace || !pointer) return;
    // HLSDK usercmd_s: lerp_msec[0:2], msec[2], pad, angles[4:16],
    // forward/sidemove/upmove[16:28], lightlevel[28], pad, buttons[30:32].
    // Read only this common prefix; never write or add a usercmd.
    if (!Readable(pointer, 32)) return;
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    unsigned short buttons = 0;
    std::memcpy(&buttons, bytes + 30, sizeof(buttons));
    Event("usercmd", buttons, bytes[2], active);
}
void Shutdown() {
    if (trace) StopTrace();
}
}
