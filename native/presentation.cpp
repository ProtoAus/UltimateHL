#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <cwchar>
#include "diagnostics.hpp"
#include "presentation.hpp"

namespace {
void* api[87]{};
UhlCvar* borderless = nullptr;
HWND window = nullptr;
LONG_PTR savedStyle = 0, savedExStyle = 0;
RECT savedRect{};
bool applied = false;
bool warningPrinted = false;
bool launcherWindowed = false, captureSafe = false;
HWND matte = nullptr;
RECT matteBounds{};
int requestedWidth=0,requestedHeight=0;
constexpr wchar_t matteClass[] = L"UltimateHLCompositorMatte";
LRESULT CALLBACK MatteProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
void UpdateMatte() {
    if (!captureSafe || !applied || GetForegroundWindow()!=window || IsIconic(window)) {
        Presentation::FocusLost();
        return;
    }
    // A sub-monitor-sized window does not always get the shell's fullscreen
    // taskbar suppression. Cover it only while active; focus messages demote
    // immediately even if the engine stops rendering in the background.
    if (!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST))
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    if (!matte) {
        WNDCLASSW cls{};
        cls.lpfnWndProc=MatteProc;cls.hInstance=GetModuleHandleW(L"UHLCompat.dll");
        cls.lpszClassName=matteClass;cls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RegisterClassW(&cls);
        matte=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,matteClass,L"",WS_POPUP,
            0,0,1,1,nullptr,nullptr,cls.hInstance,nullptr);
    }
    if (matte) SetWindowPos(matte,window,matteBounds.left,matteBounds.top,
        matteBounds.right-matteBounds.left,matteBounds.bottom-matteBounds.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}

template<class T> T Function(unsigned i) { return reinterpret_cast<T>(api[i]); }
void Print(const char* text) { if (api[30]) Function<void (__cdecl*)(const char*)>(30)(text); }
bool WindowedLaunch() {
    const wchar_t* command = GetCommandLineW();
    // Both forms are accepted by this engine. Only a startup mode is trusted;
    // changing window styles cannot convert an exclusive GL context safely.
    return wcsstr(command, L"-windowed") || wcsstr(command, L"-window");
}
BOOL CALLBACK FindWindow(HWND candidate, LPARAM output) {
    wchar_t name[64]{};
    GetClassNameW(candidate, name, 64);
    if (IsWindowVisible(candidate) && GetAncestor(candidate, GA_ROOT) == candidate && !wcscmp(name, L"Valve001")) {
        *reinterpret_cast<HWND*>(output) = candidate;
        return FALSE;
    }
    return TRUE;
}
void Restore() {
    if (matte) ShowWindow(matte,SW_HIDE);
    if (applied && IsWindow(window)) {
        SetWindowLongPtrW(window, GWL_STYLE, savedStyle);
        SetWindowLongPtrW(window, GWL_EXSTYLE, savedExStyle);
        SetWindowPos(window, HWND_NOTOPMOST, savedRect.left, savedRect.top,
            savedRect.right - savedRect.left, savedRect.bottom - savedRect.top,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    applied = false;
}
}
namespace Presentation {
void Initialize(void* functions) {
    std::memcpy(api, functions, sizeof(api));
    wchar_t mode[16]{};
    launcherWindowed = GetEnvironmentVariableW(L"UHL_BORDERLESS", mode, 16) && mode[0] == L'0';
    captureSafe = GetEnvironmentVariableW(L"UHL_CAPTURE_SAFE",mode,16) && mode[0] == L'1';
    DWORD length=GetEnvironmentVariableW(L"UHL_RENDER_WIDTH",mode,16);
    if (length && length<16) requestedWidth=static_cast<int>(std::wcstol(mode,nullptr,10));
    length=GetEnvironmentVariableW(L"UHL_RENDER_HEIGHT",mode,16);
    if (length && length<16) requestedHeight=static_cast<int>(std::wcstol(mode,nullptr,10));
    EnsureControls();
}
void EnsureControls() {
    if (!api[14] || !api[72]) return;
    borderless = Function<UhlCvar* (__cdecl*)(const char*)>(72)("uhl_borderless");
    if (!borderless) {
        static char name[] = "uhl_borderless", yes[] = "1", no[] = "0";
        // Mode comes from the current launcher; don't inherit a stale fullscreen
        // preference from another installation through the global video registry.
        borderless = Function<UhlCvar* (__cdecl*)(char*, char*, int)>(14)(name, launcherWindowed ? no : yes, 0);
    }
}
void* GameWindow() {
    if (!window || !IsWindow(window)) {
        applied = false;
        window = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), FindWindow, reinterpret_cast<LPARAM>(&window));
    }
    return window;
}
void Frame() {
    if (!GameWindow()) return;
    const bool desired = borderless && borderless->value != 0 && !launcherWindowed;
    if (!desired) { Restore(); return; }
    if (applied) { UpdateMatte(); return; }
    if (!WindowedLaunch()) {
        if (!warningPrinted) { Print("UltimateHL: borderless requires a windowed GL startup. Use UltimateHL.exe, not hl.exe directly.\n"); warningPrinted = true; }
        return;
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor)) return;
    RECT bounds = monitor.rcMonitor;
    matteBounds = bounds;
    if (captureSafe) {
        struct ScreenInfo { int size,width,height,flags,charHeight; short widths[256]; };
        ScreenInfo info{};info.size=sizeof(info);
        if (!api[12] || !Function<int (__cdecl*)(ScreenInfo*)>(12)(&info) || info.width<=0 || info.height<=0 ||
            info.width>bounds.right-bounds.left || info.height>bounds.bottom-bounds.top ||
            (info.width==bounds.right-bounds.left && info.height==bounds.bottom-bounds.top) ||
            (requestedWidth && info.width!=requestedWidth) || (requestedHeight && info.height!=requestedHeight)) {
            if (!warningPrinted) { Print("UltimateHL: capture-safe render mode was rejected or fell back; use normal borderless with OBS Windows Graphics Capture.\n");warningPrinted=true; }
            return;
        }
        bounds.left+=(bounds.right-bounds.left-info.width)/2;
        bounds.top+=(bounds.bottom-bounds.top-info.height)/2;
        bounds.right=bounds.left+info.width;bounds.bottom=bounds.top+info.height;
    }
    savedStyle = GetWindowLongPtrW(window, GWL_STYLE);
    savedExStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    GetWindowRect(window, &savedRect);
    SetWindowLongPtrW(window, GWL_STYLE, (savedStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP);
    SetWindowLongPtrW(window, GWL_EXSTYLE, savedExStyle & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
    applied = true; // Make failure rollback restore the styles too.
    if (!SetWindowPos(window, HWND_NOTOPMOST, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) { Restore(); return; }
    // This is our own ordinary window, not protected video. Never request
    // layered rendering or capture exclusion; leave compositor/capture enabled.
    SetWindowDisplayAffinity(window, WDA_NONE);
    applied = true;
    UpdateMatte();
    Print(captureSafe ? "UltimateHL: compositor-safe borderless active; supported native mode with black matte, no framebuffer scaling.\n" :
        "UltimateHL: borderless WINDOWED presentation active (not exclusive fullscreen).\n");
}
void Status() {
    Print(applied ? (captureSafe ? "Presentation: compositor-safe borderless, no scaling, capture exclusion off.\n" : "Presentation: borderless windowed, capture exclusion off.\n") :
        "Presentation: ordinary window/not applied yet (use the v3 launcher for desktop-sized borderless).\n");
}
void FocusLost() {
    if (!captureSafe) return;
    if (matte) ShowWindow(matte,SW_HIDE);
    if (IsWindow(window) && (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST))
        SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
void Shutdown() {
    Restore();
    if (matte) DestroyWindow(matte);
    matte=nullptr;
    UnregisterClassW(matteClass,GetModuleHandleW(L"UHLCompat.dll"));
    window = nullptr; borderless = nullptr;
}
}
