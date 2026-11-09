#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "cursor_guard.hpp"
#include "cursor_policy.hpp"
#include "diagnostics.hpp"
#include "engine_bridge.hpp"
#include "presentation.hpp"
namespace {
void* api[87]{};
const int* mouseActive=nullptr;
UhlCvar *confine=nullptr,*freeze=nullptr;
RECT ownedRect{};bool owned=false;
template<class T>T Fn(unsigned index){return reinterpret_cast<T>(api[index]);}
UhlCvar* Register(char* name) {
    auto* value=Fn<UhlCvar* (__cdecl*)(const char*)>(72)(name);
    static char one[]="1";
    return value ? value : Fn<UhlCvar* (__cdecl*)(char*,char*,int)>(14)(name,one,1);
}
CursorPolicy Policy() {
    HWND window=static_cast<HWND>(Presentation::GameWindow());
    const char* level=api[74] ? Fn<const char* (__cdecl*)()>(74)() : nullptr;
    return {window && GetForegroundWindow()==window && !IsIconic(window),
        EngineBridge::ConsoleOpen(),
        mouseActive && *mouseActive!=0 && level && *level,
        EngineBridge::Loading(),EngineBridge::ProtectLoadMouse(),
        confine && confine->value!=0,freeze && freeze->value!=0};
}
}
namespace CursorGuard {
void Initialize(void* functions,const int* active) {
    std::memcpy(api,functions,sizeof(api));mouseActive=active;EnsureControls();
}
void EnsureControls() {
    if (!api[14] || !api[72])return;
    static char clipName[]="uhl_cursor_lock",freezeName[]="uhl_load_mouse_freeze";
    confine=Register(clipName);freeze=Register(freezeName);
}
bool FreezeMouse() { return Policy().Freeze(); }
bool NeutralLook() { return Policy().NeutralLook(); }
void Release() {
    RECT current{};
    // Do not remove confinement installed by the newly focused application.
    if (owned && GetClipCursor(&current) && EqualRect(&current,&ownedRect)) ClipCursor(nullptr);
    owned=false;
}
void Update() {
    const auto policy=Policy();
    if (!policy.Confine()) { Release();return; }
    HWND window=static_cast<HWND>(Presentation::GameWindow());
    RECT rect{};if (!GetClientRect(window,&rect)) { Release();return; }
    POINT corners[2]{{rect.left,rect.top},{rect.right,rect.bottom}};
    if (!ClientToScreen(window,&corners[0]) || !ClientToScreen(window,&corners[1])) { Release();return; }
    rect={corners[0].x,corners[0].y,corners[1].x,corners[1].y};
    if (rect.right<=rect.left || rect.bottom<=rect.top) { Release();return; }
    if (policy.Freeze()) {
        const LONG x=rect.left+(rect.right-rect.left)/2,y=rect.top+(rect.bottom-rect.top)/2;
        rect={x,y,x+1,y+1}; // OS confinement pins both axes, independently of render FPS.
    }
    RECT current{};
    if (GetClipCursor(&current) && EqualRect(&current,&rect)) { ownedRect=rect;owned=true;return; }
    // Focus can change between querying the policy and this API call.
    if (GetForegroundWindow()==window && ClipCursor(&rect)) { ownedRect=rect;owned=true; }
}
void Status() {
    if (!api[30])return;
    char text[180]{};const auto policy=Policy();
    std::snprintf(text,sizeof(text),"Cursor: confinement=%s, currently owned=%d, load mouse freeze=%s (active=%d); focus/console/settings release.\n",
        confine && confine->value!=0 ? "on" : "off",owned,freeze && freeze->value!=0 ? "on" : "off",policy.Freeze());
    Fn<void (__cdecl*)(const char*)>(30)(text);
}
void Shutdown() { Release();mouseActive=nullptr;confine=freeze=nullptr; }
}
