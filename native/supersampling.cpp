#include "scene_target.hpp"
#include "supersampling.hpp"
#include "engine_bridge.hpp"
#include "display_presenter.hpp"
#include "hud_scaling.hpp"
#include "brightness.hpp"
#include "diagnostics.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
namespace {
std::array<void*,87> api{};
template<class T>T Fn(unsigned i) { return reinterpret_cast<T>(api[i]); }
void Print(const char* text) { if (api[30]) Fn<void (__cdecl*)(const char*)>(30)(text); }
UhlCvar *scale=nullptr,*ztrick=nullptr,*noRefresh=nullptr,*debug=nullptr,*msaa=nullptr;
HGLRC msaaContext=nullptr;bool initialMultisample=false;
using RenderFn=void (__cdecl*)();
using ViewportFn=void (APIENTRY*)(GLint,GLint,GLsizei,GLsizei);
RenderFn originalRender=nullptr;
ViewportFn originalViewport=nullptr,originalScissor=nullptr;
using BufferFn=void (APIENTRY*)(GLenum);
BufferFn originalDrawBuffer=nullptr,originalReadBuffer=nullptr;
SceneTarget target;
GLenum SceneBuffer(GLenum mode) {
    if ((target.Active() || HudScaling::Active() || DisplayPresenter::Capturing()) && mode>=GL_FRONT_LEFT && mode<=GL_FRONT_AND_BACK && mode!=GL_NONE) return 0x8ce0;
    return mode;
}
void APIENTRY DrawBuffer(GLenum mode){originalDrawBuffer(SceneBuffer(mode));}
void APIENTRY ReadBuffer(GLenum mode){originalReadBuffer(SceneBuffer(mode));}
unsigned debugErrors=0;
void CheckError(const char* stage) {
    if (!debug || debug->value==0)return;
    const GLenum error=glGetError();
    if (error!=GL_NO_ERROR && debugErrors++<12) {
        char text[160]{};std::snprintf(text,sizeof(text),"SSAA GL debug: %s error=0x%x\n",stage,error);Print(text);
    }
}
struct Patch { DWORD* slot=nullptr;DWORD before=0,after=0; };
std::array<Patch,8> patches{};
bool installed=false,attempted=false,warned=false,wasScaled=false;
unsigned frames=0,failures=0;
int outputWidth=0,outputHeight=0,startupScale=0;
const char* installReason="waiting for GL context";
bool Store(Patch& patch,bool undo) {
    if (!patch.slot) return false;
    if (*patch.slot!=(undo ? patch.after : patch.before)) return false;
    DWORD protection=0,ignored=0;
    if (!VirtualProtect(patch.slot,sizeof(DWORD),PAGE_EXECUTE_READWRITE,&protection)) return false;
    *patch.slot=undo ? patch.before : patch.after;
    VirtualProtect(patch.slot,sizeof(DWORD),protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),patch.slot,sizeof(DWORD));return true;
}
void APIENTRY Viewport(GLint x,GLint y,GLsizei w,GLsizei h) {
    if (target.Active()) originalViewport(x*2,y*2,w*2,h*2);
    else if(HudScaling::Active())originalViewport(0,0,HudScaling::Width(),HudScaling::Height());
    else originalViewport(x,y,w,h);
}
void APIENTRY Scissor(GLint x,GLint y,GLsizei w,GLsizei h) {
    if (target.Active()) originalScissor(x*2,y*2,w*2,h*2);
    else if(HudScaling::Active())originalScissor(x,y-HudScaling::HeightDelta(),w,h);
    else originalScissor(x,y,w,h);
}
void __cdecl Render() {
    Supersampling::ApplyLaunchScale();
    if (target.Active()) { originalRender();return; }
    struct ScreenInfo { int size,width,height,flags,charHeight;short widths[256]; };
    ScreenInfo info{};info.size=sizeof(info);
    const bool wanted=scale && scale->value==2 && (!noRefresh || noRefresh->value==0) && api[12] &&
        Fn<int (__cdecl*)(ScreenInfo*)>(12)(&info);
    CheckError("before capture (prior renderer/UI)");
    const bool capture=wanted && target.Begin(info.width,info.height,2);
    CheckError("target begin");
    if (wanted && !capture) {
        ++failures;
        if (!warned) { Print("UltimateHL: scene supersampling unavailable; rendering at native size. ");Print(target.Error());Print("\n");warned=true; }
    }
    // Z-trick reuses a particular framebuffer's depth history. A private scene
    // FBO (or a switch back to native) must instead get a clean normal depth pass.
    const float oldZ=ztrick ? ztrick->value : 0;
    if (ztrick && (capture || wasScaled)) ztrick->value=0;
    originalRender(); // Exactly one original scene; never a simulation frame.
    CheckError("original scene");
    if (ztrick) ztrick->value=oldZ;
    if (capture) {
        target.End();CheckError("resolve");++frames;outputWidth=info.width;outputHeight=info.height;
    }
    wasScaled=capture;
    Brightness::AfterScene(); // finished 3D view, before the HUD, console and menus
}
void Install() {
    if (attempted || !EngineBridge::Supported() || !wglGetCurrentContext()) return;
    attempted=true;
    auto* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    originalRender=reinterpret_cast<RenderFn>(base+0x946fd0);
    const DWORD sites[]{0x9c9763,0x9c97b8,0x9c97f5};
    const auto* entry=reinterpret_cast<const BYTE*>(originalRender);
    // Actual 3248 R_RenderView starts with x87 r_norefresh comparison, not
    // the unrelated 83 EC 0C A1 prologue used by the broken v3.2 validation.
    bool valid=Diagnostics::Readable(entry,20) && entry[0]==0xd9 && entry[1]==0x05 &&
        entry[6]==0xd8 && entry[7]==0x1d && !std::memcmp(entry+12,"\x83\xec\x14\xdf\xe0\xf6\xc4\x44",8);
    installReason=valid ? "renderer call operands" : "R_RenderView signature";
    for (unsigned i=0;i<3 && valid;++i) {
        auto* call=base+sites[i];std::int32_t displacement=0;
        valid=Diagnostics::Readable(call,5) && call[0]==0xe8;
        if (!valid) break;
        std::memcpy(&displacement,call+1,4);
        valid=call+5+displacement==reinterpret_cast<BYTE*>(originalRender);
        patches[i]={reinterpret_cast<DWORD*>(call+1),static_cast<DWORD>(displacement),
            static_cast<DWORD>(reinterpret_cast<BYTE*>(Render)-(call+5))};
    }
    if (valid) installReason="engine GL viewport/scissor pointers";
    auto* viewport=reinterpret_cast<ViewportFn*>(base+0x13e860c);
    auto* scissor=reinterpret_cast<ViewportFn*>(base+0x13e8870);
    HMODULE gl=GetModuleHandleW(L"opengl32.dll");
    valid=valid && gl && Diagnostics::Readable(viewport,4) && Diagnostics::Readable(scissor,4) &&
        *viewport==reinterpret_cast<ViewportFn>(GetProcAddress(gl,"glViewport")) &&
        *scissor==reinterpret_cast<ViewportFn>(GetProcAddress(gl,"glScissor"));
    if (valid) {
        originalViewport=*viewport;originalScissor=*scissor;
        patches[3]={reinterpret_cast<DWORD*>(viewport),reinterpret_cast<DWORD>(originalViewport),reinterpret_cast<DWORD>(Viewport)};
        patches[4]={reinterpret_cast<DWORD*>(scissor),reinterpret_cast<DWORD>(originalScissor),reinterpret_cast<DWORD>(Scissor)};
    }
    if (valid) {
        installReason="engine GL read/draw buffer pointers";
        const DWORD offsets[]{0x13e82b4,0x13e87cc,0x13e8558};
        const char* exports[]{"glDrawBuffer","glDrawBuffer","glReadBuffer"};
        for (unsigned i=0;i<3 && valid;++i) {
            auto* slot=reinterpret_cast<DWORD*>(base+offsets[i]);
            const auto before=reinterpret_cast<DWORD>(GetProcAddress(gl,exports[i]));
            valid=before && Diagnostics::Readable(slot,4) && *slot==before;
            if (valid)patches[i+5]={slot,before,reinterpret_cast<DWORD>(i==2 ? ReadBuffer : DrawBuffer)};
        }
        originalDrawBuffer=reinterpret_cast<BufferFn>(GetProcAddress(gl,"glDrawBuffer"));
        originalReadBuffer=reinterpret_cast<BufferFn>(GetProcAddress(gl,"glReadBuffer"));
    }
    if (valid) { installReason="writing validated operands";for (auto& patch:patches) if (!Store(patch,false)) { valid=false;break; } }
    if (!valid) {
        for (auto& patch:patches) if (patch.slot && *patch.slot==patch.after) Store(patch,true);
        Print("UltimateHL: SSAA validation failed at ");Print(installReason);Print("; native rendering preserved.\n");return;
    }
    installed=true;installReason="validated renderer and scoped GL hooks";
    Print("UltimateHL: scene-only 2x supersampling hook ready; independent HUD scale, native menus. uhl_render_scale 2 enables it.\n");
}
}
namespace Supersampling {
void EnsureControls() {
    if (!api[14] || !api[72]) return;
    const auto get=Fn<UhlCvar* (__cdecl*)(const char*)>(72);
    scale=get("uhl_render_scale");
    if (!scale) { static char name[]="uhl_render_scale",one[]="2";scale=Fn<UhlCvar* (__cdecl*)(char*,char*,int)>(14)(name,one,1); }
    debug=get("uhl_render_debug");
    if (!debug){static char name[]="uhl_render_debug",off[]="0";debug=Fn<UhlCvar* (__cdecl*)(char*,char*,int)>(14)(name,off,0);}
    msaa=get("uhl_msaa");
    if(!msaa){static char name[]="uhl_msaa",off[]="0";msaa=Fn<UhlCvar* (__cdecl*)(char*,char*,int)>(14)(name,off,1);}
    ztrick=get("gl_ztrick");noRefresh=get("r_norefresh");
    EngineBridge::EnsureCommand("uhl_apply_launch",ApplyLaunchScale);
}
void Initialize(void* functions) {
    std::memcpy(api.data(),functions,sizeof(api));EnsureControls();
    wchar_t launch[8]{};
    if (GetEnvironmentVariableW(L"UHL_START_RENDER_SCALE",launch,8) && (!std::wcscmp(launch,L"1") || !std::wcscmp(launch,L"2")))
        startupScale=launch[0]-L'0';
}
void ApplyLaunchScale() {
    if(startupScale){const int value=startupScale;startupScale=0;SetScale(value);}
}
void SetScale(int value) {
    startupScale=0;
    if ((value==1 || value==2) && api[37]) { static char name[]="uhl_render_scale";Fn<void (__cdecl*)(char*,float)>(37)(name,static_cast<float>(value)); }
}
void Frame() {
    Install();
    const HGLRC current=wglGetCurrentContext();
    if(current && msaa) {
        if(msaaContext!=current){msaaContext=current;initialMultisample=glIsEnabled(0x809d)!=0;}
        if(msaa->value!=0)glEnable(0x809d);else glDisable(0x809d);
    }
}
bool Ready(){return installed;}
void Status() {
    char text[300]{};
    std::snprintf(text,sizeof(text),"Scene SSAA: %s, requested=%.0fx, resolved=%u, fallback=%u, last SSAA scene=%dx%d -> %dx%d; HUD independent.\n",
        installed ? "ready" : "unavailable",scale ? scale->value : 1,frames,failures,outputWidth*2,outputHeight*2,outputWidth,outputHeight);
    Print(text);if(debugErrors) {std::snprintf(text,sizeof(text),"  SSAA diagnostic GL errors=%u\n",debugErrors);Print(text);}
    Print("  SSAA validation: ");Print(installReason);Print("\n");
    if (wglGetCurrentContext()) {
        GLint samples=0;glGetIntegerv(0x80a9,&samples);
        std::snprintf(text,sizeof(text),"  Output format samples=%d; MSAA rasterization=%s (uhl_msaa). FXAA/driver overrides are external.\n",samples,glIsEnabled(0x809d) ? "on" : "off");Print(text);
    }
}
void Shutdown() {
    target.Reset();
    if(msaaContext && wglGetCurrentContext()==msaaContext){if(initialMultisample)glEnable(0x809d);else glDisable(0x809d);}
    msaaContext=nullptr;
    if (installed) for (auto& patch:patches) Store(patch,true);
    installed=false;
}
}
