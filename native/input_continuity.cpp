#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <cstdio>
#include "diagnostics.hpp"
#include "input_continuity.hpp"
#include "load_key_state.hpp"
#include "presentation.hpp"
#include "engine_bridge.hpp"

namespace {
void* api[87]{};
UhlCvar* enabled = nullptr;
LoadKeyState state;
unsigned virtualKeys[256]{};
InputContinuity::MessageContext messageContext;
unsigned transitionDepth = 0;
struct Action { const char* name; ClientButton* button; };
Action actions[] = {{"+forward",nullptr},{"+back",nullptr},{"+moveleft",nullptr},{"+moveright",nullptr},
    {"+left",nullptr},{"+right",nullptr},{"+lookup",nullptr},{"+lookdown",nullptr},{"+moveup",nullptr},
    {"+movedown",nullptr},{"+jump",nullptr},{"+duck",nullptr},{"+attack",nullptr},{"+attack2",nullptr},
    {"+use",nullptr},{"+reload",nullptr},{"+speed",nullptr},{"+strafe",nullptr},{"+klook",nullptr},{"+mlook",nullptr}};
int keyActions[256]{};
bool physicalHeld[256]{}, continuousHeld[256]{};
struct Candidate {
    bool active = false, continuous = false, edge = false, reconciled = false;
    int action = 0;
    bool repaired = false, polled = false;
};
Candidate candidates[256]{};
RepairedButtonOwner repairedOwners[256]{};

template<class T> T Function(unsigned i) { return reinterpret_cast<T>(api[i]); }
void Print(const char* text) { if (api[30]) Function<void (__cdecl*)(const char*)>(30)(text); }
int FindAction(const char* binding) {
    if (!RecoverableActionName(binding)) return -1;
    if (binding) for (unsigned i = 0; i < sizeof(actions) / sizeof(Action); ++i)
        if (!std::strcmp(binding, actions[i].name)) return static_cast<int>(i);
    return -1; // Never replay arbitrary aliases, arguments or semicolon scripts.
}
void ResolveButtons() {
    auto* client = reinterpret_cast<BYTE*>(GetModuleHandleW(L"client.dll"));
    if (!client) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(client);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(client + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    BYTE* data = nullptr;
    std::size_t size = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        if (!std::memcmp(section->Name, ".data\0\0\0", 8)) { data = client + section->VirtualAddress; size = section->Misc.VirtualSize; }
    if (!data || size < sizeof(ClientButton)) return;
    const BYTE* keyDownFunction = nullptr;
    for (auto& action : actions) {
        action.button = nullptr;
        auto* function = static_cast<BYTE*>(EngineBridge::CommandHandler(action.name));
        if (!Diagnostics::Readable(function, 14) || function < client || function >= data) continue;
        // Stock push &kbutton; call KeyDown; cleanup/ret. +attack2 also
        // tail-calls the viewport helper after KeyDown; its first call and
        // button ABI are identical. Original .text is hash-pinned by the patcher.
        if (function[0] != 0x68 || function[5] != 0xe8) continue;
        const bool popReturn = function[10] == 0x59 && function[11] == 0xc3;
        const bool cleanup = function[10] == 0x83 && function[11] == 0xc4 && function[12] == 4;
        const bool attack2Tail = !std::strcmp(action.name, "+attack2") && function[13] == 0xe9;
        if (!popReturn && !(cleanup && (function[13] == 0xc3 || attack2Tail))) continue;
        std::int32_t offset = 0;
        std::memcpy(&offset, function + 6, sizeof(offset));
        const BYTE* target = function + 10 + offset;
        if (target < client || target >= data || (keyDownFunction && target != keyDownFunction)) continue;
        if (!keyDownFunction) keyDownFunction = target;
        ClientButton* button = nullptr;
        std::memcpy(&button, function + 1, sizeof(std::uint32_t));
        auto* bytes = reinterpret_cast<BYTE*>(button);
        if (bytes < data || bytes > data + size - sizeof(ClientButton) ||
            reinterpret_cast<std::uintptr_t>(bytes) % 4 || !Diagnostics::Readable(button, sizeof(ClientButton))) continue;
        action.button = button;
    }
}
unsigned FallbackVirtualKey(int key) {
    if (key >= 'a' && key <= 'z') return static_cast<unsigned>(key - 'a' + 'A');
    if (key >= '0' && key <= '9') return static_cast<unsigned>(key);
    if (key == 9 || key == 13 || key == 27 || key == 32) return static_cast<unsigned>(key);
    if (key == 127) return VK_BACK;
    if (key >= 135 && key <= 146) return VK_F1 + static_cast<unsigned>(key - 135);
    switch (key) {
        case 128: return VK_UP; case 129: return VK_DOWN; case 130: return VK_LEFT; case 131: return VK_RIGHT;
        case 132: return VK_MENU; case 133: return VK_CONTROL; case 134: return VK_SHIFT;
        case 147: return VK_INSERT; case 148: return VK_DELETE; case 149: return VK_NEXT;
        case 150: return VK_PRIOR; case 151: return VK_HOME; case 152: return VK_END;
        // Exact 3248 codes, not a mixed old/new-engine mouse range.
        case 241: return VK_LBUTTON; case 242: return VK_RBUTTON;
        case 243: return VK_MBUTTON; case 244: return VK_XBUTTON1; case 245: return VK_XBUTTON2;
        default: return 0;
    }
}
bool Physical(int key) {
    const unsigned vk = virtualKeys[key] ? virtualKeys[key] : FallbackVirtualKey(key);
    return vk && (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000);
}
bool OwnsInput() {
    DWORD process = 0;
    HWND foreground = GetForegroundWindow();
    GetWindowThreadProcessId(foreground, &process);
    return process == GetCurrentProcessId() && foreground == Presentation::GameWindow() &&
        (!api[70] || !Function<int (__cdecl*)()>(70)());
}
void Retire(int key) {
    auto& candidate = candidates[key];
    bool released = repairedOwners[key].Release(key);
    if (candidate.active && actions[candidate.action].button)
        released = ReleaseButtonOwner(*actions[candidate.action].button,key) || released;
    if (released) Diagnostics::Event("load_owner_released", key);
    candidate = {};
    physicalHeld[key] = continuousHeld[key] = false;
}
void CapturePhysical() {
    if (!state.Armed() || !enabled || enabled->value == 0 || !OwnsInput()) return;
    for (int key = 1; key < 256; ++key) {
        const bool down = Physical(key);
        if (candidates[key].active && !down) { Retire(key); continue; }
        if (!down || candidates[key].active) continue;
        const int action = FindAction(EngineBridge::KeyBinding(key));
        if (action < 0 || !actions[action].button) continue;
        // VGUI can swallow the down; binding-aware physical capture must not
        // wait for a client callback or Windows' next auto-repeat.
        candidates[key] = {true, false, true, false, action, false, true};
        keyActions[key] = action;
        physicalHeld[key] = true;
        Diagnostics::Event("load_new_physical_press", key, action);
    }
}
void RepairButtons() {
    if (!enabled || enabled->value == 0 || !OwnsInput() || !state.Armed()) return;
    for (int key = 0; key < 256; ++key) {
        auto& candidate = candidates[key];
        if (!candidate.active || !Physical(key)) continue;
        auto* button = actions[candidate.action].button;
        if (!button) continue; // Unknown code/layout is never patched speculatively.
        const int before = button->state, owner0 = button->down[0], owner1 = button->down[1];
        if (RestoreHeldButton(*button, key, candidate.continuous && !candidate.edge)) {
            // Track even an unchanged owner: its synthetic release may have been
            // consumed, so a later real release must not depend on engine replay.
            repairedOwners[key].Remember(*button,key);
            candidate.repaired = true;
            if (before != button->state || owner0 != button->down[0] || owner1 != button->down[1])
                Diagnostics::Event("load_button_repaired", key, before, button->state);
        }
    }
}
}

namespace InputContinuity {
void Initialize(void* functions) {
    std::memcpy(api, functions, sizeof(api));
    EnsureControls();
}
void EnsureControls() {
    if (!api[14] || !api[72]) return;
    enabled = Function<UhlCvar* (__cdecl*)(const char*)>(72)("uhl_preserve_load_keys");
    if (!enabled) {
        static char name[] = "uhl_preserve_load_keys", value[] = "1";
        enabled = Function<UhlCvar* (__cdecl*)(char*, char*, int)>(14)(name, value, 1);
    }
    ResolveButtons();
}
void BeginLoad() {
    if (transitionDepth++ == 0) {
        for (auto& candidate : candidates) candidate = {};
        if (enabled && enabled->value != 0 && OwnsInput()) for (int key = 0; key < 256; ++key) {
            if (!physicalHeld[key] || !Physical(key)) continue;
            const int action = keyActions[key];
            const auto* button = actions[action].button;
            candidates[key] = {true, continuousHeld[key] || (button && (button->state & 7) == 1), false, false, action};
            Diagnostics::Event("load_hold_candidate", key, action, candidates[key].continuous);
        }
    }
    state.Begin();
    CapturePhysical();
    Diagnostics::Event("load_input_begin");
}
void EndLoad() {
    if (transitionDepth) --transitionDepth;
    state.End();
    CapturePhysical();
    Diagnostics::Event("load_input_resume");
}
void BeforeCreateMove(bool active) {
    if (!enabled || enabled->value == 0) { if (state.Armed()) Cancel("input_disabled"); return; }
    if (active) { CapturePhysical(); RepairButtons(); }
}
void AfterCreateMove(bool active) {
    if (active) {
        if (OwnsInput()) for (int key = 0; key < 256; ++key) {
            if (!physicalHeld[key]) continue;
            if (!Physical(key)) { Retire(key); continue; }
            const auto* button = actions[keyActions[key]].button;
            if (button && (button->down[0] == key || button->down[1] == key) && (button->state & 1)) {
                continuousHeld[key] = true;
                if (candidates[key].active) { candidates[key].continuous = true; candidates[key].edge = false; }
            }
        }
        state.FirstCommandFinished();
        if (!state.Armed()) {
            for (auto& candidate : candidates) candidate = {};
            for (auto& key : state.keys) key.retained = false;
        }
    }
}
int ClientKey(int down, int key, const char* binding, int result) {
    if (key < 0 || key >= 256) return result;
    const int action = FindAction(binding);
    const bool eligible = action >= 0;
    if (messageContext.valid && down != 0 && messageContext.down && !state.Armed() &&
        MatchingVirtualKey(FallbackVirtualKey(key),messageContext.vk))
        virtualKeys[key] = messageContext.vk; // Never learn an unrelated/synthetic callback's VK.
    const unsigned vk = virtualKeys[key] ? virtualKeys[key] : FallbackVirtualKey(key);
    const bool actualUp = messageContext.valid && !messageContext.down && messageContext.vk == vk;
    const bool physicalDown = Physical(key);
    if (down && result && eligible) {
        keyActions[key] = action;
        if (physicalDown && OwnsInput()) physicalHeld[key] = true;
        if (state.Armed() && physicalDown && eligible && !candidates[key].active)
            candidates[key] = {true, false, true, false, action};
    }
    if (!down && (actualUp || !physicalDown)) Retire(key);
    const bool consume = state.Accept(key, down != 0, eligible, result != 0,
        enabled && enabled->value != 0, OwnsInput(), physicalDown, actualUp);
    if (consume) {
        candidates[key].reconciled = false;
        Diagnostics::Event("load_key_release_retained", key);
        return 0;
    }
    return result;
}
void Cancel(const char* reason) {
    // No synthetic engine key events, on either recovery or cancellation.
    // They can dispatch mode bindings/aliases in the current command context.
    state.Cancel();
    for (int key = 0; key < 256; ++key) Retire(key);
    for (auto& held : physicalHeld) held = false;
    for (auto& held : continuousHeld) held = false;
    Diagnostics::Event(reason);
}
bool DirectFpsMessage() {
    if (!messageContext.valid || !messageContext.down || !OwnsInput()) return false;
    for (int key=1;key<256;++key) {
        const unsigned vk=virtualKeys[key] ? virtualKeys[key] : FallbackVirtualKey(key);
        if (MatchingVirtualKey(vk,messageContext.vk) && EngineBridge::FpsBinding(EngineBridge::KeyBinding(key))) return true;
    }
    return false;
}
void Status() {
    Print(enabled && enabled->value != 0 ?
        "Held-load protection: enabled (stock +/- bindings, physical keys, focus-safe; playtest required).\n" :
        "Held-load protection: disabled (stock key clearing).\n");
    unsigned resolved = 0;
    for (const auto& action : actions) resolved += action.button != nullptr;
    Print("Input policy: load-only owner repair; saves do not replay input; klook/mlook are never synthesized.\n");
    Print(EngineBridge::BindingMapReady() ? "Load-window physical binding lookup: ready; two resumed commands protected.\n" :
                                        "Load-window physical binding lookup: unavailable (existing held-key tracking only).\n");
    char message[128]{};
    sprintf_s(message, "Validated stock action states: %u/%u (no guessed input offsets).\n", resolved, static_cast<unsigned>(sizeof(actions) / sizeof(Action)));
    Print(message);
    if (actions[18].button && actions[19].button) {
        char modes[180]{};
        std::snprintf(modes,sizeof(modes),"Look modes: klook state=%d owners=(%d,%d); mlook state=%d. Recovery never writes these.\n",
            actions[18].button->state,actions[18].button->down[0],actions[18].button->down[1],actions[19].button->state);
        Print(modes);
    }
    for (const auto& action : actions) if (!action.button) { Print("  state repair not installed for "); Print(action.name); Print(" (nonstandard handler).\n"); }
}
void Shutdown() { Cancel("input_shutdown"); enabled = nullptr; }
MessageScope::MessageScope(unsigned message, std::uintptr_t wParam, std::uintptr_t lParam) : previous_(messageContext) {
    messageContext = {};
    const bool keyboard = message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP;
    if (keyboard) messageContext = {true, message == WM_KEYDOWN || message == WM_SYSKEYDOWN, static_cast<unsigned>(wParam)};
    else switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
        messageContext = {true, message != WM_LBUTTONUP, VK_LBUTTON}; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
        messageContext = {true, message != WM_RBUTTONUP, VK_RBUTTON}; break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
        messageContext = {true, message != WM_MBUTTONUP, VK_MBUTTON}; break;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
        messageContext = {true, message != WM_XBUTTONUP, static_cast<unsigned>(HIWORD(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2)}; break;
    default: break;
    }
    if (messageContext.valid) {
        const bool actualEdge = !keyboard || !messageContext.down || !(lParam & (std::uintptr_t(1) << 30));
        if (actualEdge) for (int key = 0; key < 256; ++key) {
            const unsigned vk = virtualKeys[key] ? virtualKeys[key] : FallbackVirtualKey(key);
            if (vk != messageContext.vk) continue;
            // Accepted gameplay bindings need not receive HUD_Key_Event on UP
            // in 3248. Observe real WM releases/presses here, not just callbacks.
            // A queued down may follow our poll of the SAME physical press.
            if (messageContext.down && (candidates[key].polled || repairedOwners[key].Active()) && physicalHeld[key] && Physical(key)) {
                candidates[key].polled = false;
                continue;
            }
            Retire(key);
            state.keys[static_cast<unsigned>(key)] = {};
        }
        if (keyboard && messageContext.down && wParam==VK_ESCAPE) EngineBridge::AllowMenu();
        if (keyboard && messageContext.down && (wParam == VK_ESCAPE || wParam == VK_OEM_3)) Cancel("input_menu_console_cancel");
    } else if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wParam)) Cancel("input_focus_cancel");
}
MessageScope::~MessageScope() { messageContext = previous_; }
}
