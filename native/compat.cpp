#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <string>

#include "raw_motion.hpp"
#include "precision_mouse.hpp"
#include "diagnostics.hpp"
#include "engine_bridge.hpp"
#include "input_continuity.hpp"
#include "presentation.hpp"
#include "mouse_settings.hpp"
#include "rendering.hpp"
#include "cursor_guard.hpp"
#include "settings.hpp"
#include "display_presenter.hpp"
#include "practice_cheats.hpp"
#include "hud_scaling.hpp"
#include "brightness.hpp"

// This module supports ONLY the fingerprinted, unencrypted valve_WON client.
// The patch tool stores original RVAs in a new section. Movement/physics code,
// engine timing and saved-game formats are unchanged. rendering.cpp separately
// validates and redirects renderer-only call/data operands in runtime memory.
struct ClientProfile {
    char magic[8];
    std::uint32_t version, size;
    std::uint32_t initialize, accumulate, createMove, frame, shutdown;
    std::uint32_t clearStates, keyEvent, mouseEvent, hudInit;
    std::uint32_t mouseActive, getCursorIat, setCursorIat;
};
static_assert(sizeof(ClientProfile) == 64);
static_assert(sizeof(void*) == 4, "GoldSrc requires a 32-bit build");

using Cvar = UhlCvar;

namespace {
using VoidFn = void (__cdecl*)();
using InitializeFn = int (__cdecl*)(void*, int);
using MoveFn = void (__cdecl*)(float, void*, int);
using FrameFn = void (__cdecl*)(double);
using CenterFn = int (__cdecl*)();
using RegisterFn = Cvar* (__cdecl*)(char*, char*, int);
using AddCommandFn = int (__cdecl*)(char*, VoidFn);
using PrintFn = void (__cdecl*)(const char*);
using GetCursorFn = BOOL (WINAPI*)(LPPOINT);
using SetCursorFn = BOOL (WINAPI*)(int, int);

BYTE* client = nullptr;
const ClientProfile* profile = nullptr;
void* engine[87] = {};
Cvar* rawCvar = nullptr;
HWND window = nullptr;
DWORD windowThread = 0;
bool registered = false;
bool registrationConflict = false;
bool unsupportedDevice = false;
unsigned inputDepth = 0;
RawMotion motion;
GetCursorFn originalGetCursor = nullptr;
SetCursorFn originalSetCursor = nullptr;
std::wstring startupReceiptPath;

constexpr UINT_PTR subclassId = 0x55484c31;
constexpr UINT_PTR cursorTimerId = 0x55484c33;
constexpr char profileMagic[8] = {'U', 'H', 'L', 'R', 'A', 'W', '3', '\0'};

// Indices of the original client's engine API v7, verified against its
// Initialize copy (87 entries) and the SDK cl_enginefuncs_s declaration.
template<class T> T Engine(unsigned index) {
    return reinterpret_cast<T>(engine[index]);
}
template<class T> T Original(std::uint32_t rva) {
    return reinterpret_cast<T>(client + rva);
}
void Print(const char* text) {
    if (engine[30]) Engine<PrintFn>(30)(text);
    OutputDebugStringA(text);
}

bool ResolveProfile() {
    if (profile) return true;
    client = reinterpret_cast<BYTE*>(GetModuleHandleW(L"client.dll"));
    if (!client) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(client);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(client + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        return false;
    const auto imageSize = nt->OptionalHeader.SizeOfImage;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".uhl\0\0\0\0", 8) != 0) continue;
        if (section->VirtualAddress > imageSize - sizeof(ClientProfile)) return false;
        auto* p = reinterpret_cast<const ClientProfile*>(client + section->VirtualAddress);
        if (std::memcmp(p->magic, profileMagic, 8) || p->version != 3 || p->size != sizeof(*p))
            return false;
        const std::uint32_t rvas[] = {p->initialize, p->accumulate, p->createMove,
            p->frame, p->shutdown, p->clearStates, p->keyEvent, p->mouseEvent, p->hudInit,
            p->mouseActive, p->getCursorIat, p->setCursorIat};
        for (auto rva : rvas) if (rva == 0 || rva > imageSize - 4) return false;
        profile = p;
        return true;
    }
    return false;
}

bool Active() {
    const bool active = !CursorGuard::FreezeMouse() && registered && rawCvar && rawCvar->value != 0 &&
        window && GetForegroundWindow() == window &&
        *Original<const int*>(profile->mouseActive) != 0 &&
        !EngineBridge::ConsoleOpen();
    motion.SetActive(active);
    return active;
}

BOOL WINAPI RawGetCursorPos(LPPOINT point) {
    // Crucially, NO process-wide GetCursorPos hook. Only the client's IAT
    // points here, and only its IN_Accumulate/CL_CreateMove calls use raw data.
    // VGUI, console and engine menu coordinates stay in Windows screen space.
    if (point && inputDepth && engine[32] && engine[33] && CursorGuard::NeutralLook()) {
        motion.Clear();
        point->x=Engine<CenterFn>(32)();point->y=Engine<CenterFn>(33)();return TRUE;
    }
    if (point && inputDepth && Active()) {
        const auto delta = motion.Take();
        point->x = Engine<CenterFn>(32)() + delta.first;
        point->y = Engine<CenterFn>(33)() + delta.second;
        return TRUE;
    }
    return originalGetCursor(point);
}
BOOL WINAPI RawSetCursorPos(int x, int y) {
    // Never warp the desktop pointer from a background/settings/console input pass.
    if (inputDepth && window && CursorGuard::NeutralLook() && !CursorGuard::FreezeMouse()) return TRUE;
    if (CursorGuard::FreezeMouse()) {
        CursorGuard::Update();
        return originalSetCursor(Engine<CenterFn>(32)(),Engine<CenterFn>(33)());
    }
    return originalSetCursor(x, y);
}

// Expected-value check avoids undoing another tool's hook on shutdown.
bool ReplaceSlot(std::uint32_t rva, void* expected, void* replacement) {
    auto** slot = Original<void**>(rva);
    if (*slot != expected) return false;
    DWORD oldProtection;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) return false;
    *slot = replacement;
    DWORD ignored;
    VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
    return true;
}

bool MouseRegistrationExists() {
    UINT count = 0;
    if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
        return true; // Fail closed: don't take over another tool's registration.
    if (!count) return false;
    auto* devices = static_cast<RAWINPUTDEVICE*>(HeapAlloc(GetProcessHeap(), 0,
                                                        count * sizeof(RAWINPUTDEVICE)));
    if (!devices) return true;
    const UINT result = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
    bool found = result == UINT(-1);
    if (!found) for (UINT i = 0; i < result; ++i)
        found |= devices[i].usUsagePage == 1 && devices[i].usUsage == 2;
    HeapFree(GetProcessHeap(), 0, devices);
    return found;
}

void ReleaseRegistration() {
    if (registered) {
        // Remove only a registration that still belongs to this window.
        UINT count = 0;
        if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != UINT(-1) && count) {
            auto* devices = static_cast<RAWINPUTDEVICE*>(HeapAlloc(GetProcessHeap(), 0,
                                                                count * sizeof(RAWINPUTDEVICE)));
            if (devices) {
                UINT result = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
                if (result != UINT(-1)) for (UINT i = 0; i < result; ++i) {
                    if (devices[i].usUsagePage == 1 && devices[i].usUsage == 2 &&
                        devices[i].hwndTarget == window && devices[i].dwFlags == 0) {
                        RAWINPUTDEVICE remove{1, 2, RIDEV_REMOVE, nullptr};
                        RegisterRawInputDevices(&remove, 1, sizeof(remove));
                        break;
                    }
                }
                HeapFree(GetProcessHeap(), 0, devices);
            }
        }
    }
    registered = false;
    motion.SetActive(false);
}

LRESULT CALLBACK WindowInput(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                             UINT_PTR, DWORD_PTR) {
    InputContinuity::MessageScope inputMessage(message, wParam, lParam);
    if (message==WM_TIMER && wParam==cursorTimerId) { CursorGuard::Update();Active();return 0; }
    CursorGuard::Update();
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN ||
        message == WM_KEYUP || message == WM_SYSKEYUP)
        Diagnostics::Event((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) ? "win_key_down" : "win_key_up",
                           wParam, ((lParam >> 16) & 0x1ff) | (((lParam >> 30) & 1) << 16),
                           static_cast<DWORD>(GetTickCount() - static_cast<DWORD>(GetMessageTime())));
    if (message >= WM_LBUTTONDOWN && message <= WM_MOUSEHWHEEL && message != WM_MOUSEMOVE)
        Diagnostics::Event("win_mouse_message", message, wParam, lParam);
    if (InputContinuity::DirectFpsMessage()) return message==WM_XBUTTONDOWN ? TRUE : 0;
    if (message==WM_KEYDOWN && wParam=='C' && (GetKeyState(VK_CONTROL)&0x8000) && !(lParam&(1<<30)) &&
        EngineBridge::CopyConsoleSelection()) Diagnostics::Event("console_copy"); // the key still reaches GameUI
    if (message == WM_INPUT && GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT && Active()) {
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT result = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                                           &input, &size, sizeof(RAWINPUTHEADER));
        if (result != UINT(-1) && result >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE) &&
            input.header.dwType == RIM_TYPEMOUSE) {
            if (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                unsupportedDevice = true; // Relative mice only; no guessed tablet conversion.
            } else {
                motion.Add(input.data.mouse.lLastX, input.data.mouse.lLastY);
            }
        }
    } else if (message == WM_KILLFOCUS ||
               (message == WM_ACTIVATEAPP && !wParam)) {
        motion.SetActive(false);
        CursorGuard::Release();
        Presentation::FocusLost();
        Diagnostics::Event("focus_lost", message);
    } else if (message == WM_NCDESTROY) {
        KillTimer(hwnd,cursorTimerId);CursorGuard::Release();
        Presentation::FocusLost();
        ReleaseRegistration();
        RemoveWindowSubclass(hwnd, WindowInput, subclassId);
        window = nullptr;
        windowThread = 0;
    }
    // No RIDEV_NOLEGACY, INPUTSINK or cursor emulation. Only an explicit
    // single-command uhl_fps binding is consumed above, never gameplay buttons. DefSubclassProc also provides required WM_INPUT cleanup.
    const LRESULT result=DefSubclassProc(hwnd, message, wParam, lParam);
    if (window) CursorGuard::Update();
    return result;
}

void TryAttachWindow() {
    if (!profile) return;
    if (!window) {
        HWND candidate = static_cast<HWND>(Presentation::GameWindow());
        DWORD process = 0;
        DWORD thread = GetWindowThreadProcessId(candidate, &process);
        if (!candidate || process != GetCurrentProcessId() || thread != GetCurrentThreadId()) return;
        if (!SetWindowSubclass(candidate, WindowInput, subclassId, 0)) return;
        window = candidate;
        windowThread = thread;
        SetTimer(window,cursorTimerId,10,nullptr);
    }
    if (registered || registrationConflict || !rawCvar || rawCvar->value == 0 || !originalGetCursor) return;
    if (MouseRegistrationExists()) {
        registrationConflict = true;
        Print("UltimateHL: another raw-mouse registration exists; NOT taking it over. Remove RInput and restart.\n");
        return;
    }
    RAWINPUTDEVICE device{1, 2, 0, window};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) return;
    registered = true;
    motion.SetActive(false);
    Print("UltimateHL: raw mouse registered (legacy UI input preserved).\n");
}

void Status() {
    Print("UltimateHL v3.7.1: fractional mouse precision, load-only button repair, Big AG art / 2x scene defaults, filtered HUD scales, Tankfird BXT and GPU DXGI output.\n");
    Print(IsProcessDPIAware() ? "DPI awareness: enabled.\n" : "DPI awareness: MISSING; use UltimateHL.exe and restart.\n");
    MouseSettings::Status(Print);
    CursorGuard::Status();
    EngineBridge::Status();
    InputContinuity::Status();
    Presentation::Status();
    Rendering::Status();
    DisplayPresenter::Status();
    PracticeCheats::Status();
    HudScaling::Status();
    Brightness::Status();
    Print(PrecisionMouse::Installed() ? "Fractional mouse precision: enabled (including sensitivity below 1).\n" : "Fractional mouse precision: unavailable (stock integer scaling).\n");
    Print(originalGetCursor ? "Client mouse adapter: installed.\n" : "Client mouse adapter: unavailable (stock input).\n");
    Print(registered ? "Raw mouse registration: ready.\n" : "Raw mouse registration: not ready (stock input).\n");
    Print(rawCvar && rawCvar->value != 0 ? "uhl_rawinput: enabled.\n" : "uhl_rawinput: disabled.\n");
    if (registrationConflict) Print("Conflict: another module registered raw mouse. Restart without RInput.\n");
    if (unsupportedDevice) Print("Warning: absolute-position mouse/tablet packets were ignored; use a relative mouse.\n");
}
void Help() {
    Print("UltimateHL v3.7.1 commands (requires the valve_WON client, not just hl.exe.manifest):\n");
    Print("  uhl_help, uhl_status, uhl_diagnostics, uhl_trace_start, uhl_trace_stop, uhl_render_test, uhl_frame_dump\n");
    Print("  uhl_settings -- open the local settings panel (also on main menu)\n");
    Print("Cursor: uhl_cursor_lock 0/1; uhl_load_mouse_freeze 0/1. Alt-Tab/console/settings release.\n");
    Print("AA off: uhl_render_scale 1; uhl_msaa 0. Driver FXAA/forced overrides are external. No stock gl_msaa cvar.\n");
    Print("Renderer: uhl_native_textures 0/1 (restart assets); uhl_render_scale 1/2 (scene only). HUD stays native.\n");
    Print("Brightness: uhl_brightness -0.9..3 brightens the picture, not lightmaps (0 = off). uhl_gamma_lock 1 keeps gamma/lightgamma/texgamma\n");
    Print("  and legacy brightness at defaults; while locked, brightness N sets uhl_brightness N-1.\n");
    Print("  uhl_fps <1..100> -- explicit bindings bypass menu; a load floor stays active while the choice is queued for gameplay\n");
    Print("Settings: uhl_rawinput 0/1; uhl_preserve_load_keys 0/1; uhl_borderless 0/1; uhl_load_fps_min 0/20..100; uhl_load_menu 0/1\n");
    Print("Defaults: exec uhl_defaults.cfg (load FPS floor 0, gl_spriteblend 0, gl_round_down 0, gl_picmip 0, nearest mip filtering).\n");
    Print("Local practice: sv_cheats 1 works in the current single-player map; uhl_cheats_live 0 restores stock permission latching. Never auto-enables sv_cheats.\n");
    Print("Borderless needs a windowed desktop-size startup: use UltimateHL.exe. It also verifies native-client loading.\n");
    Print("Mouse: fractional filter/sensitivity scaling avoids dropped slow movement at sensitivity 0.1; uhl_status reports validation.\n");
    Print("Windows mouse acceleration/EPP and pointer-speed writes are always blocked, including the game's exit restore.\n");
    Print("BXT: v3.7 ships Tankfird BXT rebuilt from source; it loads this adapter for the unmodified client.dll. A client patched on disk also runs with the official DLL.\n");
    Print("Console: Ctrl+C copies the selected console text. HUD scale enlarges health/armor/ammo and BXT timer/speedometer; text stays 1x.\n");
    if (engine[71]) { Print("Active game directory: "); Print(Engine<const char* (__cdecl*)()>(71)()); Print("\n"); }
}
void RegisterPublicCommands() {
    EngineBridge::EnsureControls();
    Diagnostics::EnsureCommands();
    InputContinuity::EnsureControls();
    Presentation::EnsureControls();
    Rendering::EnsureControls();
    CursorGuard::EnsureControls();
    Brightness::EnsureControls();
    Settings::EnsureCommands();
    EngineBridge::EnsureCommand("uhl_render_test", Rendering::SelfTest);
    EngineBridge::EnsureCommand("uhl_frame_dump", DisplayPresenter::DumpFrame);
    EngineBridge::EnsureCommand("uhl_status", Status);
    EngineBridge::EnsureCommand("uhl_help", Help);
    if (engine[72]) rawCvar = Engine<Cvar* (__cdecl*)(const char*)>(72)("uhl_rawinput");
    if (!rawCvar) {
        static char name[] = "uhl_rawinput", value[] = "1";
        rawCvar = Engine<RegisterFn>(14)(name, value, 1);
    }
}
void WriteStartupReceipt(bool hudReady) {
    wchar_t token[64]{};
    const DWORD length = GetEnvironmentVariableW(L"UHL_LAUNCH_TOKEN", token, 64);
    if (!length || length >= 64 || std::wcslen(token) != length) return;
    for (const auto ch : token) if (ch && !(ch >= L'0' && ch <= L'9') && ch != L'-') return;
    const std::wstring path = std::wstring(L"uhl-startup-") + token + L".txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"w") || !file) return;
    fprintf(file, "adapter_version=3\npid=%lu\nhud_ready=%d\ndpi_aware=%d\n", GetCurrentProcessId(), hudReady, IsProcessDPIAware());
    fprintf(file, "windows_mouse_settings=%s\n", MouseSettings::Protected() ? "protected" : "NOT protected");
    fprintf(file, "mouse_precision=%s\n", PrecisionMouse::Installed() ? "enabled" : "unavailable");
    if (engine[71]) fprintf(file, "game=%s\n", Engine<const char* (__cdecl*)()>(71)());
    const char* commands[] = {"uhl_help", "uhl_status", "uhl_diagnostics", "uhl_trace_start", "uhl_trace_stop", "uhl_fps"};
    for (const auto* command : commands) fprintf(file, "%s=%s\n", command, EngineBridge::HasCommand(command) ? "registered" : "not verified");
    fclose(file);
    startupReceiptPath = path;
}
void AppendShutdownReceipt() {
    if (startupReceiptPath.empty()) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, startupReceiptPath.c_str(), L"a") || !file) return;
    fprintf(file, "mouse_settings_shutdown=%s\nblocked_mouse_writes=%u\n",
            MouseSettings::Protected() ? "protected" : "NOT protected", MouseSettings::BlockedWrites());
    fclose(file);
}
struct InputScope {
    InputScope() { TryAttachWindow(); Active(); ++inputDepth; }
    ~InputScope() { --inputDepth; }
};
} // namespace

extern "C" int __cdecl UHL_Initialize(void* funcs, int version) {
    if (!ResolveProfile()) return 0;
    // Before original initialization, not merely before the first mouse frame.
    // This IAT hook stays installed through the original client's shutdown.
    if (!MouseSettings::Install(client)) {
        OutputDebugStringA("UltimateHL: refusing client initialization without Windows mouse-settings protection.\n");
        return 0;
    }
    const int result = Original<InitializeFn>(profile->initialize)(funcs, version);
    if (!result || version != 7) return result;
    std::memcpy(engine, funcs, sizeof(engine));
    if (!engine[14] || !engine[17] || !engine[32] || !engine[33]) return result;
    EngineBridge::Initialize(funcs);
    Diagnostics::Initialize(funcs);
    Presentation::Initialize(funcs);
    CursorGuard::Initialize(funcs,Original<const int*>(profile->mouseActive));
    Settings::Initialize(funcs);
    DisplayPresenter::Initialize(funcs);
    PracticeCheats::Initialize(funcs);
    HudScaling::Initialize(funcs);
    Brightness::Initialize(funcs);
    InputContinuity::Initialize(funcs);
    Rendering::Initialize(funcs);
    RegisterPublicCommands();
    if (!PrecisionMouse::Install(client))
        Print("UltimateHL: fractional mouse patch did not validate; keeping stock integer scaling.\n");
    WriteStartupReceipt(false);
    originalGetCursor = *Original<GetCursorFn*>(profile->getCursorIat);
    originalSetCursor = *Original<SetCursorFn*>(profile->setCursorIat);
    const bool getInstalled = ReplaceSlot(profile->getCursorIat,
        reinterpret_cast<void*>(originalGetCursor), reinterpret_cast<void*>(RawGetCursorPos));
    const bool setInstalled = getInstalled && ReplaceSlot(profile->setCursorIat,
        reinterpret_cast<void*>(originalSetCursor), reinterpret_cast<void*>(RawSetCursorPos));
    if (!setInstalled) {
        if (getInstalled) ReplaceSlot(profile->getCursorIat,
            reinterpret_cast<void*>(RawGetCursorPos), reinterpret_cast<void*>(originalGetCursor));
        originalGetCursor = nullptr;
        Print("UltimateHL: mouse adapter could not be installed; keeping stock input.\n");
        return result;
    }
    Print("UltimateHL v3.7.1 loaded. uhl_help lists actual commands. Use UltimateHL.exe to select the native valve_WON client.\n");
    return result;
}
extern "C" void __cdecl UHL_HudInit() {
    if (!ResolveProfile()) return;
    Original<VoidFn>(profile->hudInit)();
    RegisterPublicCommands(); // Correct lifecycle stage, after original client registration.
    Presentation::Frame();
    TryAttachWindow();
    WriteStartupReceipt(true);
}
extern "C" void __cdecl UHL_Accumulate() {
    if (!ResolveProfile()) return;
    InputScope scope;
    Original<VoidFn>(profile->accumulate)();
}
extern "C" void __cdecl UHL_CreateMove(float time, void* cmd, int active) {
    if (!ResolveProfile()) return;
    InputScope scope;
    EngineBridge::BeforeCreateMove(active != 0);
    InputContinuity::BeforeCreateMove(active != 0);
    Original<MoveFn>(profile->createMove)(time, cmd, active);
    Diagnostics::UserCommand(cmd, active != 0);
    InputContinuity::AfterCreateMove(active != 0);
}
extern "C" void __cdecl UHL_Frame(double time) {
    if (!ResolveProfile()) return;
    PracticeCheats::Frame();
    EngineBridge::Frame();
    Presentation::Frame();
    Rendering::Frame();
    HudScaling::Frame();
    Brightness::Frame();
    DisplayPresenter::Frame();
    TryAttachWindow();
    CursorGuard::Update();
    Active();
    Original<FrameFn>(profile->frame)(time);
}
extern "C" void __cdecl UHL_ClearStates() {
    if (!ResolveProfile()) return;
    Diagnostics::Event("client_clear_states");
    EngineBridge::ClientClearStates();
    motion.Clear();
    Original<VoidFn>(profile->clearStates)();
}
extern "C" int __cdecl UHL_KeyEvent(int down, int key, const char* binding) {
    if (!ResolveProfile()) return 0;
    const int original = Original<int (__cdecl*)(int, int, const char*)>(profile->keyEvent)(down, key, binding);
    const int result = InputContinuity::ClientKey(down, key, binding, original);
    Diagnostics::Event("client_key", key, down, result);
    return result;
}
extern "C" void __cdecl UHL_MouseEvent(int buttons) {
    if (!ResolveProfile()) return;
    Diagnostics::Event("client_mouse_buttons", buttons);
    Original<void (__cdecl*)(int)>(profile->mouseEvent)(buttons);
}
extern "C" void __cdecl UHL_Shutdown() {
    if (!ResolveProfile()) return;
    Settings::Shutdown();
    CursorGuard::Shutdown();
    if (window) KillTimer(window,cursorTimerId);
    InputContinuity::Shutdown();
    HudScaling::Shutdown();
    Brightness::Shutdown();
    DisplayPresenter::Shutdown();
    Rendering::Shutdown();
    Presentation::Shutdown();
    PracticeCheats::Shutdown();
    EngineBridge::Shutdown();
    Diagnostics::Shutdown();
    ReleaseRegistration();
    if (window && windowThread == GetCurrentThreadId())
        RemoveWindowSubclass(window, WindowInput, subclassId);
    window = nullptr;
    windowThread = 0;
    if (originalGetCursor) {
        ReplaceSlot(profile->getCursorIat, reinterpret_cast<void*>(RawGetCursorPos),
                    reinterpret_cast<void*>(originalGetCursor));
        ReplaceSlot(profile->setCursorIat, reinterpret_cast<void*>(RawSetCursorPos),
                    reinterpret_cast<void*>(originalSetCursor));
    }
    PrecisionMouse::Shutdown();
    Original<VoidFn>(profile->shutdown)();
    AppendShutdownReceipt(); // Includes the original client's attempted exit restore.
}

// Which HUD pass is drawing, for BXT's own HUD (see HudScaling::Pass).
extern "C" int __cdecl UHL_HudScalePass() { return HudScaling::Pass(); }

// Legit-run layout: the stock client.dll stays unmodified on disk. BXT calls this as soon as
// the client is loaded, before the engine (or BXT) looks up its exports, and it does in memory
// what tools/patch_client.py does to the file: the nine adapted exports jump through stubs in
// the client's own code padding to UHL_*, and the profile holds the original export RVAs, the
// mouse-active variable and the GetCursorPos/SetCursorPos import slots.
namespace {
ClientProfile runtimeProfile{};
void* stubTargets[9]{};
const BYTE stockClient[32]={0xd2,0x94,0x56,0x9c,0x06,0x76,0x8e,0xb8,0x19,0xcb,0x02,0x67,0xe8,0xbf,0x72,0x50,
    0x4a,0x21,0xc7,0x34,0xe3,0x87,0xf6,0xe0,0x79,0x78,0x41,0xff,0x53,0x88,0xdf,0xff}; // patch_client.py CLIENT_SHA256
constexpr std::uint32_t stockMouseActive=0x748E4; // patch_client.py MOUSE_ACTIVE_RVA, valid for this client only
}
extern "C" int __cdecl UHL_AttachClient(HMODULE module) {
    if (profile || !module) return 0;
    auto* base=reinterpret_cast<BYTE*>(module);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE) return 0;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386) return 0;
    const DWORD imageSize=nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* text=nullptr;
    auto* section=IMAGE_FIRST_SECTION(nt);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++section) {
        if (!std::memcmp(section->Name,".uhl\0\0\0\0",8)) return 0; // a patched client loads this adapter itself
        if (!std::memcmp(section->Name,".text\0\0\0",8)) text=section;
    }
    wchar_t path[MAX_PATH]{};
    if (!text || !GetModuleFileNameW(module,path,MAX_PATH) || !EngineBridge::FileMatches(path,stockClient)) return 0;
    const auto& exportDir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const auto& importDir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!exportDir.VirtualAddress || !importDir.VirtualAddress) return 0;

    auto* exports=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+exportDir.VirtualAddress);
    auto* functions=reinterpret_cast<DWORD*>(base+exports->AddressOfFunctions);
    auto* names=reinterpret_cast<const DWORD*>(base+exports->AddressOfNames);
    auto* ordinals=reinterpret_cast<const WORD*>(base+exports->AddressOfNameOrdinals);
    const char* const adapted[9]={"Initialize","IN_Accumulate","CL_CreateMove","HUD_Frame","HUD_Shutdown",
        "IN_ClearStates","HUD_Key_Event","IN_MouseEvent","HUD_Init"};
    void* const adapters[9]={reinterpret_cast<void*>(UHL_Initialize),reinterpret_cast<void*>(UHL_Accumulate),
        reinterpret_cast<void*>(UHL_CreateMove),reinterpret_cast<void*>(UHL_Frame),reinterpret_cast<void*>(UHL_Shutdown),
        reinterpret_cast<void*>(UHL_ClearStates),reinterpret_cast<void*>(UHL_KeyEvent),reinterpret_cast<void*>(UHL_MouseEvent),
        reinterpret_cast<void*>(UHL_HudInit)};
    std::uint32_t ClientProfile::* const fields[9]={&ClientProfile::initialize,&ClientProfile::accumulate,
        &ClientProfile::createMove,&ClientProfile::frame,&ClientProfile::shutdown,&ClientProfile::clearStates,
        &ClientProfile::keyEvent,&ClientProfile::mouseEvent,&ClientProfile::hudInit};
    ClientProfile p{};DWORD* entries[9]{};
    for (int i=0;i<9;++i) {
        for (DWORD n=0;n<exports->NumberOfNames && !entries[i];++n)
            if (!std::strcmp(reinterpret_cast<const char*>(base+names[n]),adapted[i])) entries[i]=&functions[ordinals[n]];
        if (!entries[i]) return 0;
        p.*fields[i]=*entries[i];
    }
    for (auto* d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+importDir.VirtualAddress);d->Name;++d) {
        if (_stricmp(reinterpret_cast<const char*>(base+d->Name),"user32.dll") || !d->OriginalFirstThunk) continue;
        auto* lookup=reinterpret_cast<const IMAGE_THUNK_DATA*>(base+d->OriginalFirstThunk);
        for (unsigned i=0;lookup[i].u1.AddressOfData;++i) {
            if (IMAGE_SNAP_BY_ORDINAL(lookup[i].u1.Ordinal)) continue;
            const char* name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+lookup[i].u1.AddressOfData)->Name;
            const auto slot=static_cast<std::uint32_t>(d->FirstThunk+i*sizeof(IMAGE_THUNK_DATA));
            if (!std::strcmp(name,"GetCursorPos")) p.getCursorIat=slot;
            else if (!std::strcmp(name,"SetCursorPos")) p.setCursorIat=slot;
        }
    }
    std::memcpy(p.magic,profileMagic,8);p.version=3;p.size=sizeof(p);p.mouseActive=stockMouseActive;
    const std::uint32_t rvas[]={p.initialize,p.accumulate,p.createMove,p.frame,p.shutdown,p.clearStates,
        p.keyEvent,p.mouseEvent,p.hudInit,p.mouseActive,p.getCursorIat,p.setCursorIat};
    for (auto rva:rvas) if (rva==0 || rva>imageSize-4) return 0;

    // "jmp [target]" stubs in the zero padding at the end of .text's last page, which is mapped
    // executable; the export entries then still point inside the client image.
    constexpr DWORD stubSize=8,stubBytes=9*stubSize;
    const DWORD end=text->VirtualAddress+text->Misc.VirtualSize,stubs=((end+0xfffu)&~0xfffu)-stubBytes;
    if (stubs<end) return 0;
    BYTE* code=base+stubs;
    for (DWORD i=0;i<stubBytes;++i) if (code[i]) return 0;
    DWORD old=0,ignored=0;
    if (!VirtualProtect(code,stubBytes,PAGE_EXECUTE_READWRITE,&old)) return 0;
    for (int i=0;i<9;++i) {
        stubTargets[i]=adapters[i];
        BYTE* stub=code+i*stubSize;const auto target=reinterpret_cast<std::uintptr_t>(&stubTargets[i]);
        stub[0]=0xff;stub[1]=0x25;std::memcpy(stub+2,&target,4);stub[6]=stub[7]=0x90;
    }
    VirtualProtect(code,stubBytes,old,&ignored);FlushInstructionCache(GetCurrentProcess(),code,stubBytes);
    const SIZE_T table=exports->NumberOfFunctions*sizeof(DWORD);
    if (!VirtualProtect(functions,table,PAGE_READWRITE,&old)) return 0; // stubs stay unreferenced
    for (int i=0;i<9;++i) *entries[i]=stubs+i*stubSize;
    VirtualProtect(functions,table,old,&ignored);
    runtimeProfile=p;client=base;profile=&runtimeProfile;
    return 1;
}
