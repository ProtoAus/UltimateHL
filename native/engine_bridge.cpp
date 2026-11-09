#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <cwchar>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include "diagnostics.hpp"
#include "engine_bridge.hpp"
#include "load_fps_guard.hpp"
#include "transition_policy.hpp"
#include "input_continuity.hpp"
#include "cursor_guard.hpp"

namespace {
using CommandFn = void (__cdecl*)();
struct Command { Command* next; const char* name; CommandFn function; int flags; };
static_assert(sizeof(Command) == 16);
void* api[87]{};
Command* load = nullptr;
Command* reload = nullptr;
Command* save = nullptr;
Command** commandHead = nullptr;
CommandFn originalLoad = nullptr;
CommandFn originalReload = nullptr;
CommandFn originalSave = nullptr;
struct OwnedCommand { const char* name; CommandFn handler; };
OwnedCommand owned[64]{};
unsigned ownedCount = 0;
UhlCvar *minimumFps = nullptr, *loadMenu = nullptr;
LoadFpsSession loading;
bool beganInGame=false, menuCancelled=false, menuReset=false, uiWarned=false;
bool uiPending=false,sawInactiveMove=false,guiLoading=false;
ULONGLONG lastActiveTick=0;
ULONGLONG loadStarted=0;
void* gameUi=nullptr;
void** gameUiTable=nullptr;
void* gameConsole=nullptr;bool (__thiscall* consoleVisible)(void*)=nullptr;
using CopyFn=void (__thiscall*)(void*);CopyFn copyHistory=nullptr,copyEntry=nullptr;BYTE* gameUiBase=nullptr;
using ActivateUiFn=int (__thiscall*)(void*);
ActivateUiFn originalActivateUi=nullptr;
using LoadingUiFn=void (__thiscall*)(void*,const char*,const char*);
LoadingUiFn originalLoadingStarted=nullptr,originalLoadingFinished=nullptr;
void BeginUiLoading();
void EndUiLoading();
void __fastcall LoadingStarted(void* self,void*,const char* resource,const char* level) {
    BeginUiLoading();originalLoadingStarted(self,resource,level);
}
void __fastcall LoadingFinished(void* self,void*,const char* resource,const char* level) {
    originalLoadingFinished(self,resource,level);EndUiLoading();
}
std::uint64_t fpsRevision = 0;
unsigned loadDepth = 0;
bool supported = false;

template<class T> T Function(unsigned i) { return reinterpret_cast<T>(api[i]); }
void Print(const char* text) {
    if (api[30]) Function<void (__cdecl*)(const char*)>(30)(text);
}
bool FileFingerprint(const wchar_t* path,const BYTE (&expected)[32]) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE;
    ok = ok && CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    BYTE buffer[16384];
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = CryptHashData(hash, buffer, count, 0) != FALSE;
    }
    BYTE digest[32]{};
    DWORD size = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) != FALSE;
    ok = ok && size == sizeof(expected) && !std::memcmp(digest, expected, sizeof(expected));
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}
bool KnownEngine() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,path,MAX_PATH)) return false;
    auto* slash=wcsrchr(path,L'\\');if (!slash) return false;slash[1]=0;
    if (wcscat_s(path,L"hw.dll")) return false;
    const BYTE expected[32]={0x52,0x5b,0xdb,0xdd,0xf2,0xf1,0x80,0x82,0x49,0x44,0xee,0x25,0x76,0x70,0x2e,0x04,
        0x0c,0x2f,0x55,0x54,0xca,0x42,0x90,0xf4,0x0f,0xae,0xc8,0x42,0xe2,0xcf,0x02,0xbb};
    return FileFingerprint(path,expected);
}
bool Executable(const void* pointer) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) return true;
    // The original pre-DEP loader executes its blob from the EXE's .data
    // reservation. Without BXT these pages can legitimately remain RW when
    // DEP is disabled. Do not change their protections or accept arbitrary RW
    // pointers outside the fingerprinted engine image.
    DWORD policy = 0;
    BOOL permanent = FALSE;
    if (!GetProcessDEPPolicy(GetCurrentProcess(), &policy, &permanent) || (policy & PROCESS_DEP_ENABLE)) return false;
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= reinterpret_cast<std::uintptr_t>(base) &&
           address < reinterpret_cast<std::uintptr_t>(base) + nt->OptionalHeader.SizeOfImage;
}
Command* FindCommands() {
    // Exact HL-4554/3248 Cmd_FindCmd pattern, also identified for this engine
    // in the user's BXT log. Newer undocumented API tail slots are NOT assumed
    // to exist: the startup probe showed they are unavailable in 3248.
    constexpr int pattern[] = {0x56,0x8b,0x35,-1,-1,-1,-1,0x85,0xf6,0x57,0x74,-1,
        0x8b,0x7c,0x24,-1,0x8b,0x46,-1,0x50,0x57,0xe8,-1,-1,-1,-1,0x83,0xc4,0x08,
        0x85,0xc0,0x74,-1,0x8b,0x36,0x85,0xf6,0x75,-1,0x5f,0x33,0xc0,0x5e,0xc3,
        0x8b,0xc6,0x5f,0x5e,0xc3,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x56};
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto end = reinterpret_cast<std::uintptr_t>(base) + nt->OptionalHeader.SizeOfImage;
    const BYTE* match = nullptr;
    auto address = reinterpret_cast<std::uintptr_t>(base);
    while (address < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info))) return nullptr;
        const auto regionEnd = std::min(end, reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize);
        if (regionEnd <= address) return nullptr;
        if (Diagnostics::Readable(reinterpret_cast<void*>(address), 1) &&
            regionEnd - address >= sizeof(pattern) / sizeof(int)) {
            for (auto at = address; at + sizeof(pattern) / sizeof(int) <= regionEnd; ++at) {
                const auto* bytes = reinterpret_cast<const BYTE*>(at);
                if (bytes[0] != 0x56 || bytes[1] != 0x8b || bytes[2] != 0x35) continue;
                bool equal = true;
                for (unsigned i = 3; i < sizeof(pattern) / sizeof(int); ++i)
                    if (pattern[i] >= 0 && bytes[i] != pattern[i]) { equal = false; break; }
                if (equal) {
                    if (match) return nullptr; // Ambiguity disables the bridge.
                    match = bytes;
                }
            }
        }
        address = regionEnd;
    }
    if (!match) return nullptr;
    Command** head = nullptr;
    std::memcpy(&head, match + 3, sizeof(head));
    if (!Diagnostics::Readable(head, sizeof(*head))) return nullptr;
    commandHead = head;
    return *head;
}
Command* FindCommand(const char* name) {
    if (!commandHead || !Diagnostics::Readable(commandHead, sizeof(*commandHead))) return nullptr;
    unsigned count = 0;
    for (Command* node = *commandHead; node; node = node->next) {
        if (++count > 8192 || !Diagnostics::Readable(node, sizeof(Command)) || !node->name) return nullptr;
        unsigned length = 0;
        for (; length < 64; ++length) {
            if (!Diagnostics::Readable(node->name + length, 1)) return nullptr;
            if (!node->name[length]) break;
        }
        if (length == 64) return nullptr;
        if (!std::strcmp(node->name, name)) return node;
    }
    return nullptr;
}
bool Replace(Command* node, CommandFn expected, CommandFn replacement) {
    if (!node || node->function != expected) return false;
    DWORD oldProtection, ignored;
    if (!VirtualProtect(&node->function, sizeof(node->function), PAGE_READWRITE, &oldProtection)) return false;
    node->function = replacement;
    VirtualProtect(&node->function, sizeof(node->function), oldProtection, &ignored);
    return true;
}
UhlCvar* Fps() {
    return api[72] ? Function<UhlCvar* (__cdecl*)(const char*)>(72)("fps_max") : nullptr;
}
void SetFps(float value) {
    if (!api[37]) return;
    if (auto* fps=Fps()) if (fps->value==value) return;
    static char name[] = "fps_max";
    Function<void (__cdecl*)(char*, float)>(37)(name, value);
}
bool SuppressMenu() {
    return uiPending && beganInGame && !menuCancelled && loadMenu && loadMenu->value==0;
}
int __fastcall ActivateGameUi(void* self,void*) {
    if (SuppressMenu()) { Diagnostics::Event("load_menu_blocked");return 1; }
    return originalActivateUi(self);
}
bool UiSlot(unsigned index,void* expected,void* replacement) {
    if (!gameUiTable || !Diagnostics::Readable(gameUiTable+index,sizeof(void*)) || gameUiTable[index]!=expected) return false;
    DWORD old=0,ignored=0;
    if (!VirtualProtect(gameUiTable+index,sizeof(void*),PAGE_READWRITE,&old)) return false;
    gameUiTable[index]=replacement;
    VirtualProtect(gameUiTable+index,sizeof(void*),old,&ignored);return true;
}
void TryGameUi() {
    if (!supported || gameUi || uiWarned) return;
    HMODULE module=GetModuleHandleW(L"GameUI.dll");if (!module) return;
    wchar_t path[MAX_PATH]{};
    const BYTE expected[32]={0x2f,0x14,0x24,0xc3,0xe9,0xa4,0x71,0xc5,0xb9,0x2d,0x42,0xaa,0xaa,0xd6,0xe6,0x32,
        0x27,0xfd,0x6a,0xf5,0x96,0x79,0x15,0x48,0x2c,0x48,0xc7,0x50,0x28,0xa8,0xce,0xd9};
    auto factory=reinterpret_cast<void* (__cdecl*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));
    if (!factory || !GetModuleFileNameW(module,path,MAX_PATH) || !FileFingerprint(path,expected)) { uiWarned=true;return; }
    auto* base=reinterpret_cast<BYTE*>(module);
    // The ~ console in this engine is GameUI's window, which Con_IsVisible does not report.
    // GameConsole003 here: Initialize=2, IsConsoleVisible=5 (a getter, false until the console
    // exists), Printf=6, after the virtual destructor.
    if (void* console=factory("GameConsole003",nullptr);!gameConsole && Diagnostics::Readable(console,sizeof(void*))) {
        auto** consoleTable=*static_cast<void***>(console);
        if (Diagnostics::Readable(consoleTable,9*sizeof(void*)) && consoleTable[2]==base+0x21a60 &&
            consoleTable[5]==base+0x21c30 && consoleTable[6]==base+0x21b70) {
            gameConsole=console;consoleVisible=reinterpret_cast<bool (__thiscall*)(void*)>(consoleTable[5]);
        }
    }
    // Right-click Copy runs these "DoCopySelected" handlers (thunks to each panel's virtual
    // CopySelected): RichText's for the log, TextEntry's for the input line.
    const BYTE historyThunk[]{0x8b,0x01,0xff,0xa0,0x1c,0x02,0x00,0x00},entryThunk[]{0x8b,0x01,0xff,0xa0,0x70,0x02,0x00,0x00};
    if (gameConsole && Diagnostics::Readable(base+0x4f2b0,8) && Diagnostics::Readable(base+0x55610,8) &&
        !std::memcmp(base+0x55610,historyThunk,8) && !std::memcmp(base+0x4f2b0,entryThunk,8)) {
        copyHistory=reinterpret_cast<CopyFn>(base+0x55610);copyEntry=reinterpret_cast<CopyFn>(base+0x4f2b0);gameUiBase=base;
    }
    void* object=factory("GameUI007",nullptr);
    if (!Diagnostics::Readable(object,sizeof(void*))) { uiWarned=true;return; }
    auto** table=*static_cast<void***>(object);
    // GameUI007 here includes a virtual destructor: Activate=4, Hide=10.
    if (!Diagnostics::Readable(table,14*sizeof(void*)) || table[4]!=base+0x22cd0 ||
        table[10]!=base+0x22d60 || table[11]!=base+0x22cb0 || table[12]!=base+0x232e0 || table[13]!=base+0x23350) { uiWarned=true;return; }
    gameUiTable=table;originalActivateUi=reinterpret_cast<ActivateUiFn>(table[4]);
    originalLoadingStarted=reinterpret_cast<LoadingUiFn>(table[12]);originalLoadingFinished=reinterpret_cast<LoadingUiFn>(table[13]);
    const bool hooked=UiSlot(4,table[4],reinterpret_cast<void*>(ActivateGameUi)) &&
        UiSlot(12,table[12],reinterpret_cast<void*>(LoadingStarted)) && UiSlot(13,table[13],reinterpret_cast<void*>(LoadingFinished));
    if (!hooked) {
        UiSlot(4,reinterpret_cast<void*>(ActivateGameUi),reinterpret_cast<void*>(originalActivateUi));
        UiSlot(12,reinterpret_cast<void*>(LoadingStarted),reinterpret_cast<void*>(originalLoadingStarted));
        UiSlot(13,reinterpret_cast<void*>(LoadingFinished),reinterpret_cast<void*>(originalLoadingFinished));
        gameUiTable=nullptr;uiWarned=true;return;
    }
    gameUi=object;
    Print("UltimateHL: exact GameUI007 load-menu activation guard ready.\n");
}
void HideLoadingMenu() {
    if (!SuppressMenu() || !gameUi || loadDepth || guiLoading) return;
    const auto* level=api[74] ? Function<const char* (__cdecl*)()>(74)() : nullptr;
    if (level && *level && (!menuReset || reinterpret_cast<bool (__thiscall*)(void*)>(gameUiTable[11])(gameUi))) {
        reinterpret_cast<void (__thiscall*)(void*)>(gameUiTable[10])(gameUi);menuReset=true;
    }
}
void ApplyLoadingFps() {
    auto* fps=Fps();if (!loading.active || !fps) return;
    loading.Observe(fps->value);
    const float value=loading.Effective(minimumFps ? minimumFps->value : 0);
    if (std::isfinite(value) && value>0 && fps->value!=value) SetFps(value);
    loading.Applied(fps->value);
}
void FinishLoading(const char* event) {
    if (!loading.active) return;
    if (auto* fps=Fps()) loading.Observe(fps->value);
    const float value=loading.Finish();
    if (std::isfinite(value) && value>0) SetFps(value);
    Diagnostics::Event(event,static_cast<int>(value));
}
void BeginUiLoading() {
    if (guiLoading) return;
    if (!uiPending) {
        // Remember actual active play across the engine's disconnect/asset-I/O
        // gap. LevelName can already be empty and slow disks can exceed seconds.
        beganInGame=lastActiveTick!=0;
        menuCancelled=false;menuReset=false;sawInactiveMove=false;loadStarted=GetTickCount64();
        if (auto* fps=Fps()) if (std::isfinite(fps->value) && fps->value>0) loading.Begin(fps->value);
    }
    guiLoading=true;uiPending=true;InputContinuity::BeginLoad();ApplyLoadingFps();CursorGuard::Update();
    Diagnostics::Event("load_ui_begin",beganInGame,loadDepth);
    RECT clip{};GetClipCursor(&clip);
    Diagnostics::Event("load_ui_mouse",CursorGuard::FreezeMouse(),clip.right-clip.left);
    Diagnostics::Event("load_ui_clip",clip.right-clip.left,clip.bottom-clip.top);
}
void EndUiLoading() {
    if (!guiLoading)return;
    guiLoading=false;InputContinuity::EndLoad();HideLoadingMenu();CursorGuard::Update();
    Diagnostics::Event("load_ui_finished",beganInGame,loadDepth);
    const char* level=api[74] ? Function<const char* (__cdecl*)()>(74)() : nullptr;
    Print("Loading lifecycle finished: ");Print(level && *level ? level : "(none)");Print("\n");
}
void ExecuteLoad(CommandFn original, int kind) {
    if (!original) return;
    const bool outermost=loadDepth++==0;
    auto* fps=Fps();
    if (outermost && fps && std::isfinite(fps->value) && fps->value>0) {
        sawInactiveMove=false;
        if (!uiPending) {
            const char* level=api[74] ? Function<const char* (__cdecl*)()>(74)() : nullptr;
            beganInGame=level && *level;menuCancelled=false;menuReset=false;
        }
        uiPending=true;
        loading.Begin(fps->value);loadStarted=GetTickCount64();TryGameUi();ApplyLoadingFps();
        CursorGuard::Update();
    }
    InputContinuity::BeginLoad();
    Diagnostics::Event("load_begin",kind,fps ? static_cast<int>(fps->value) : -1,loadDepth);
    original(); // Same thread, exactly once; no recursive Cbuf or fake physics frames.
    Diagnostics::Event("load_return",kind,fps ? static_cast<int>(fps->value) : -1,loadDepth);
    --loadDepth;
    if (outermost) HideLoadingMenu();
    InputContinuity::EndLoad();
    if (outermost) { ApplyLoadingFps();Diagnostics::Event("load_tail_begin",kind,fps ? static_cast<int>(fps->value) : -1); }
}
void RequestedFps(float value) {
    ++fpsRevision;
    if (loading.active) {
        loading.Select(value);
        const float effective=loading.Effective(minimumFps ? minimumFps->value : 0);
        SetFps(effective);if (auto* fps=Fps()) loading.Applied(fps->value);
    } else SetFps(value);
    Diagnostics::Event("fps_selection",static_cast<int>(value),loading.active,fpsRevision);
}
void Load() { ExecuteLoad(originalLoad, 0); }
void Reload() { ExecuteLoad(originalReload, 1); }
void Save() {
    // Saving is not a disconnect/reconnect transition. Never reconcile or
    // synthesize keys here: that can latch keyboard-look and steal W/S.
    Diagnostics::Event("save_begin");
    originalSave();
    Diagnostics::Event("save_return");
}
void SelectFps() {
    if (!api[38] || !api[39] || !api[37]) return;
    const int argc = Function<int (__cdecl*)()>(38)();
    if (argc != 2) { Print("Usage: uhl_fps <1..100>. A floored load stays fast; the choice applies to gameplay.\n"); return; }
    const auto* text = Function<char* (__cdecl*)(int)>(39)(1);
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (text == end || *end || !std::isfinite(value) || value < 1 || value > 100) {
        Print("UltimateHL: FPS must be a finite number from 1 through 100; no simulation cap is bypassed.\n"); return;
    }
    RequestedFps(value);
}
}

namespace EngineBridge {
void EnsureControls() {
    if (!api[14] || !api[72]) return;
    minimumFps = Function<UhlCvar* (__cdecl*)(const char*)>(72)("uhl_load_fps_min");
    if (!minimumFps) {
        static char minimum[] = "uhl_load_fps_min", value[] = "0";
        minimumFps = Function<UhlCvar* (__cdecl*)(char*, char*, int)>(14)(minimum, value, 1);
    }
    loadMenu=Function<UhlCvar* (__cdecl*)(const char*)>(72)("uhl_load_menu");
    if (!loadMenu) { static char name[]="uhl_load_menu",off[]="0";loadMenu=Function<UhlCvar* (__cdecl*)(char*,char*,int)>(14)(name,off,1); }
    TryGameUi();
    EnsureCommand("uhl_fps", SelectFps); // Exists even if the optional load bridge is unavailable.
}
void Frame() {
    TryGameUi();
    if (!uiPending && !loading.active) return;
    if (!loadDepth && GetTickCount64()-loadStarted>15000) {
        EndUiLoading();FinishLoading("load_fps_timeout");menuCancelled=true;uiPending=false;return;
    }
    HideLoadingMenu();
    if (!loading.active) return;
    ApplyLoadingFps();
    if (auto* fps=Fps()) Diagnostics::Event("load_fps_frame",static_cast<int>(fps->value),static_cast<int>(loading.desired),loadDepth);
}
void ClientClearStates() {
    // The supplied traces have a post-return inactive interval followed by a
    // late clear just before active play. Prefer this pre-command signal over
    // waiting until the active usercmd's frame time has already been selected.
    if (!loadDepth && !guiLoading && loading.active && sawInactiveMove) FinishLoading("load_fps_clear_resume");
}
void BeforeCreateMove(bool active) {
    if(active)lastActiveTick=GetTickCount64();
    if (!loadDepth && uiPending) {
        if (!active) sawInactiveMove=true;
        else if(TransitionMayResume(active,static_cast<unsigned>(loadDepth),guiLoading)) {
            HideLoadingMenu();FinishLoading("load_fps_gameplay_fallback");uiPending=false;
            CursorGuard::Update();
        }
    }
}
void AllowMenu() { menuCancelled=true; }
bool FpsBinding(const char* binding) {
    float value=0;
    if (!supported || !ParseFpsBinding(binding,value)) return false;
    RequestedFps(value);Diagnostics::Event("fps_binding_direct",static_cast<int>(value));return true;
}
bool Supported() { return supported; }
bool FileMatches(const wchar_t* path,const unsigned char (&sha256)[32]) { return FileFingerprint(path,sha256); }
bool SupportedBxt(void* module) {
    wchar_t path[MAX_PATH]{};if(!module || !GetModuleFileNameW(static_cast<HMODULE>(module),path,MAX_PATH))return false;
    const BYTE expected[32]={0xee,0x5c,0xc8,0xc3,0x20,0x89,0xc3,0x14,0x50,0x49,0x8c,0xf7,0xac,0x4c,0xab,0xe5,
        0xb8,0xae,0x53,0x2a,0x4c,0xde,0x36,0x8f,0x4a,0xd4,0xe4,0x62,0xc5,0x0c,0xd1,0xfa};
    return FileFingerprint(path,expected);
}
bool SupportedServer(void* module) {
    wchar_t path[MAX_PATH]{};if(!module || !GetModuleFileNameW(static_cast<HMODULE>(module),path,MAX_PATH))return false;
    const BYTE expected[32]={0x10,0x8a,0xb4,0x9e,0xcd,0x7d,0x09,0x65,0xe8,0x37,0x43,0xeb,0xa2,0xb1,0x5c,0x2d,0xfa,0x87,0x9d,0x66,0x95,0x30,0x34,0x4c,0xbf,0x8e,0xc5,0x16,0x91,0x29,0x2a,0x94};
    return FileFingerprint(path,expected);
}
bool Loading() { return uiPending || loadDepth!=0 || guiLoading; }
bool CopyConsoleSelection() {
    // The console log (dialog+0x114) passes keyboard focus to the input line (dialog+0x118),
    // so it never sees Ctrl+C. Copy what right-click Copy would: the log's selection, else the
    // input line's. Both panels must carry this GameUI.dll's console vtables.
    if (!copyHistory || !consoleVisible(gameConsole)) return false;
    auto* dialog=*reinterpret_cast<BYTE**>(static_cast<BYTE*>(gameConsole)+8);
    if (!Diagnostics::Readable(dialog,0x11c)) return false;
    void* history=*reinterpret_cast<void**>(dialog+0x114);void* entry=*reinterpret_cast<void**>(dialog+0x118);
    if (!Diagnostics::Readable(history,4) || !Diagnostics::Readable(entry,4) ||
        *static_cast<void**>(history)!=gameUiBase+0x1254bc || *static_cast<void**>(entry)!=gameUiBase+0x1256f4) return false;
    const DWORD before=GetClipboardSequenceNumber();
    copyHistory(history);
    if (GetClipboardSequenceNumber()==before) copyEntry(entry); // nothing selected in the log
    return true;
}
bool ConsoleOpen() {
    if (api[70] && Function<int (__cdecl*)()>(70)()) return true; // the old drop-down console
    if (gameConsole && consoleVisible(gameConsole)) return true; // GameUI's console window
    return gameUi && reinterpret_cast<bool (__thiscall*)(void*)>(gameUiTable[11])(gameUi); // GameUI menus
}
bool ProtectLoadMouse() { return beganInGame && !menuCancelled; }
static const char* const* KeyBindings() {
    if (!supported || !api[48]) return nullptr;
    // Exact 3248 Key_Event: mov ecx,[eax*4+keybindings]. BXT may detour
    // the entry, but this instruction remains in the original body.
    const auto* instruction = static_cast<const BYTE*>(api[48]) + 0x88;
    if (!Diagnostics::Readable(instruction, 7) || instruction[0] != 0x8b || instruction[1] != 0x0c || instruction[2] != 0x85) return nullptr;
    const char* const* bindings = nullptr;
    std::memcpy(&bindings, instruction + 3, sizeof(std::uint32_t));
    return Diagnostics::Readable(bindings, 256 * sizeof(char*)) ? bindings : nullptr;
}
bool BindingMapReady() { return KeyBindings() != nullptr; }
const char* KeyBinding(int key) {
    if (key < 0 || key > 255) return nullptr;
    const auto* bindings = KeyBindings();
    if (!bindings) return nullptr;
    const char* binding = bindings[key];
    if (!binding) return nullptr;
    // Only expose a bounded string; no executing/passing arbitrary memory.
    for (unsigned i = 0; i < 256; ++i) {
        if (!Diagnostics::Readable(binding + i, 1)) return nullptr;
        if (!binding[i]) return binding;
    }
    return nullptr;
}
bool HasCommand(const char* name) { return FindCommand(name) != nullptr; }
void* CommandHandler(const char* name) {
    Command* command = FindCommand(name);
    return command ? reinterpret_cast<void*>(command->function) : nullptr;
}
bool ReplaceCommand(const char* name,void* expected,void* replacement) {
    return Replace(FindCommand(name),reinterpret_cast<CommandFn>(expected),reinterpret_cast<CommandFn>(replacement));
}
bool EnsureCommand(const char* name, void (__cdecl* handler)()) {
    if (Command* existing = FindCommand(name)) return existing->function == handler;
    if (!commandHead) for (unsigned i = 0; i < ownedCount; ++i)
        if (!std::strcmp(owned[i].name, name)) return true;
    bool recorded = false;
    for (unsigned i = 0; i < ownedCount; ++i) recorded |= !std::strcmp(owned[i].name, name);
    if (!recorded) {
        if (ownedCount == 64) return false;
        owned[ownedCount++] = {name, handler};
    }
    if (!api[17]) return false;
    Function<int (__cdecl*)(char*, CommandFn)>(17)(const_cast<char*>(name), handler);
    return commandHead ? HasCommand(name) : true;
}
void Initialize(void* functions) {
    std::memcpy(api, functions, sizeof(api));
    commandHead = nullptr;
    EnsureControls();
    // Never read the newer API tail merely because an older client uses v7.
    if (!KnownEngine()) {
        Print("UltimateHL: engine command bridge disabled (not the fingerprinted 3248 engine).\n"); return;
    }
    Command* first = FindCommands();
    if (!first) { Print("UltimateHL: unique 3248 command-list signature unavailable; load hook disabled.\n"); return; }
    unsigned count = 0;
    bool saveFound = false, mapFound = false;
    for (Command* node = first; node; node = node->next) {
        if (++count > 8192 || !Diagnostics::Readable(node, sizeof(Command))) return;
        const char* text = node->name;
        if (!text || !Diagnostics::Readable(text, 1)) return;
        // Read each character before comparing; never strlen an unchecked pointer.
        unsigned length = 0;
        for (; length < 64; ++length) {
            if (!Diagnostics::Readable(text + length, 1)) return;
            if (!text[length]) break;
        }
        if (length == 64) return;
        if (!std::strcmp(text, "load")) load = node;
        if (!std::strcmp(text, "reload")) reload = node;
        if (!std::strcmp(text, "save")) { save = node; saveFound = Executable(reinterpret_cast<void*>(node->function)); }
        if (!std::strcmp(text, "map")) mapFound = Executable(reinterpret_cast<void*>(node->function));
    }
    if (!saveFound || !mapFound || !load || !Executable(reinterpret_cast<void*>(load->function))) {
        Print("UltimateHL: load command ABI could not be validated; no engine command changed.\n"); return;
    }
    originalLoad = load->function;
    if (!Replace(load, originalLoad, Load)) return;
    if (reload && Executable(reinterpret_cast<void*>(reload->function))) {
        originalReload = reload->function;
        if (!Replace(reload, originalReload, Reload)) { originalReload = nullptr; reload = nullptr; }
    }
    originalSave = save->function;
    if (!Replace(save, originalSave, Save)) { originalSave = nullptr; save = nullptr; }
    supported = true;
    EnsureControls();
    Print("UltimateHL: 3248 load bridge ready. uhl_load_fps_min defaults to 0 (no load FPS override); 100 is opt-in.\n");
}
void Status() {
    const char* level=api[74] ? Function<const char* (__cdecl*)()>(74)() : nullptr;
    Print("Level: ");Print(level && *level ? level : "(none)");Print("\n");
    Print(supported ? "Load bridge ready: common GameUI loading lifecycle covers trigger changelevels and save-loads.\n" :
                      "Load command bridge unavailable; engine load commands are stock.\n");
    char status[160]{};auto* fps=Fps();
    std::snprintf(status,sizeof(status),"FPS: current=%.1f, gameplay choice=%.1f, load/tail active=%d, synchronous depth=%d.\n",
        fps ? fps->value : 0,loading.active ? loading.desired : (fps ? fps->value : 0),loading.active,loadDepth);
    Print(status);
    Print(gameUi ? "Load-menu guard: exact GameUI007 ready; uhl_load_menu 0 suppresses in-game load activation. Escape opts out.\n" :
                   "Load-menu guard unavailable: unknown GameUI is untouched.\n");
    Print(gameConsole ? "Console detection: GameUI console window and menus (mouse look stays off while open).\n" :
                        "Console detection: drop-down console only (GameUI console not validated).\n");
    Print(copyHistory ? "Console copy: Ctrl+C copies the selected console text.\n" : "Console copy: right-click Copy only (GameUI not validated).\n");
}
void Shutdown() {
    FinishLoading("load_fps_shutdown");
    EndUiLoading();
    UiSlot(4,reinterpret_cast<void*>(ActivateGameUi),reinterpret_cast<void*>(originalActivateUi));
    UiSlot(12,reinterpret_cast<void*>(LoadingStarted),reinterpret_cast<void*>(originalLoadingStarted));
    UiSlot(13,reinterpret_cast<void*>(LoadingFinished),reinterpret_cast<void*>(originalLoadingFinished));
    gameUi=nullptr;gameUiTable=nullptr;originalActivateUi=nullptr;
    if (supported) {
        Replace(load, Load, originalLoad);
        if (originalReload) Replace(reload, Reload, originalReload);
        if (originalSave) Replace(save, Save, originalSave);
    }
    supported = false;
    commandHead = nullptr;
    ownedCount = 0;
}
}
