#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "rendering.hpp"
#include "supersampling.hpp"
#include "diagnostics.hpp"
#include "engine_bridge.hpp"
#include "texture_math.hpp"

namespace {
using DimensionsFn = void (__cdecl*)(int*, int*, int, int);
using MipFn = void (__cdecl*)(unsigned char*, int, int);
using UploadFn = void (__cdecl*)(unsigned char*, int, int, int, int, int);
std::array<void*,87> api{};
DimensionsFn originalDimensions = nullptr;
MipFn originalMip = nullptr;
UploadFn originalUpload = nullptr;
UhlCvar *nativeTextures = nullptr, *picmip = nullptr, *roundDown = nullptr, *maximum = nullptr;
UhlCvar *hudTexels=nullptr,*spriteBlend=nullptr;
double hudInset=0.5;
float hudOrigin=0.5f;
bool installed = false, warned = false;
unsigned nativeUploads = 0, safeMips = 0;
struct Patch { DWORD* operand = nullptr; DWORD before = 0, after = 0; };
std::array<Patch,8> patches{};
template<class Fn> Fn Function(unsigned index) { return reinterpret_cast<Fn>(api[index]); }
void Print(const char* text) { if (api[30]) Function<void (__cdecl*)(const char*)>(30)(text); }
UhlCvar* Cvar(const char* name) { return api[72] ? Function<UhlCvar* (__cdecl*)(const char*)>(72)(name) : nullptr; }
UhlCvar* Register(char* name, char* value) {
    if (auto* existing = Cvar(name)) return existing;
    return api[14] ? Function<UhlCvar* (__cdecl*)(char*,char*,int)>(14)(name,value,1) : nullptr;
}
bool Replace(DWORD* operand, DWORD expected, DWORD replacement) {
    if (!Diagnostics::Readable(operand,4) || *operand != expected) return false;
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(operand,4,PAGE_EXECUTE_READWRITE,&old)) return false;
    std::memcpy(operand,&replacement,4);
    FlushInstructionCache(GetCurrentProcess(),operand,4);
    VirtualProtect(operand,4,old,&ignored);
    return true;
}
void __cdecl Dimensions(int* width, int* height, int sourceWidth, int sourceHeight) {
    originalDimensions(width,height,sourceWidth,sourceHeight);
    if (nativeTextures && nativeTextures->value != 0 && picmip && roundDown && maximum &&
        NativeTextureSize(sourceWidth,sourceHeight,picmip->value,roundDown->value,maximum->value)) {
        if (*width != sourceWidth || *height != sourceHeight) ++nativeUploads;
        *width = sourceWidth; *height = sourceHeight;
    }
}
void __cdecl Mipmap(unsigned char* pixels, int width, int height) {
    if (PowerOfTwo(width) && PowerOfTwo(height)) { originalMip(pixels,width,height); return; }
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
    const bool luminance = format == 1 || format == GL_LUMINANCE || (format >= 0x803f && format <= 0x8042);
    if (SafeMipInPlace(pixels,width,height,luminance ? 1 : 4)) ++safeMips;
    else originalMip(pixels,width,height);
}
bool NpotSupported() {
    if (!wglGetCurrentContext()) return false;
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    return (version && version[0] >= '2' && version[0] <= '9') ||
        (extensions && std::strstr(extensions,"GL_ARB_texture_non_power_of_two"));
}
bool CallPatch(Patch& patch, BYTE* site, const void* original, const void* replacement) {
    if (!Diagnostics::Readable(site,5) || site[0] != 0xe8) return false;
    DWORD offset = 0; std::memcpy(&offset,site+1,4);
    if (reinterpret_cast<DWORD>(site+5)+offset != reinterpret_cast<DWORD>(original)) return false;
    patch = {reinterpret_cast<DWORD*>(site+1),offset,reinterpret_cast<DWORD>(replacement)-reinterpret_cast<DWORD>(site+5)};
    return true;
}
void TryInstall() {
    if (installed || warned || !EngineBridge::Supported() || !NpotSupported()) return;
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    // Exact decrypted 3248 renderer layout, qualified by the engine SHA-256 in
    // EngineBridge. These are rendering call operands, never physics routines.
    originalDimensions = reinterpret_cast<DimensionsFn>(base+0x93e5f0);
    originalMip = reinterpret_cast<MipFn>(base+0x93ebf0);
    originalUpload = reinterpret_cast<UploadFn>(base+0x93ee50);
    const BYTE uploadSignature[]{0x83,0xec,0x14,0x53,0x55,0x56,0x8d,0x44,0x24,0x0c,0x57};
    bool valid = Diagnostics::Readable(reinterpret_cast<void*>(originalUpload),sizeof(uploadSignature)) &&
        !std::memcmp(reinterpret_cast<void*>(originalUpload),uploadSignature,sizeof(uploadSignature));
    valid = valid && CallPatch(patches[0],base+0x93ef3d,reinterpret_cast<void*>(originalDimensions),reinterpret_cast<void*>(Dimensions));
    valid = valid && CallPatch(patches[1],base+0x93f116,reinterpret_cast<void*>(originalMip),reinterpret_cast<void*>(Mipmap));
    // Nearest HUD needs an almost-full texel span, not N-1 stretched across N.
    // A small INSIDE inset guards neighboring atlas cells without compression.
    const DWORD sites[]{0x93dbf8,0x93dc14,0x93dc45,0x93dc5d};
    for (unsigned i=0;i<4 && valid;++i) {
        auto* site=base+sites[i];DWORD before=0;
        valid=Diagnostics::Readable(site,6) && site[0]==0xdc && site[1]==(i%2 ? 0x25 : 0x05);
        if (!valid)break;
        std::memcpy(&before,site+2,4);
        valid=before==reinterpret_cast<DWORD>(base+0xa87588) && Diagnostics::Readable(reinterpret_cast<void*>(before),8) &&
            *reinterpret_cast<const double*>(before)==0.5;
        patches[i+2]={reinterpret_cast<DWORD*>(site+2),before,reinterpret_cast<DWORD>(&hudInset)};
    }
    const DWORD origins[]{0x93dcc8,0x93dcd6};
    for(unsigned i=0;i<2 && valid;++i) {
        auto* site=base+origins[i];DWORD before=0;
        valid=Diagnostics::Readable(site,6) && site[0]==0xd8 && site[1]==0x05;
        if(!valid)break;
        std::memcpy(&before,site+2,4);
        valid=before==reinterpret_cast<DWORD>(base+0xa87d7c) && Diagnostics::Readable(reinterpret_cast<void*>(before),4) &&
            *reinterpret_cast<const float*>(before)==0.5f;
        patches[i+6]={reinterpret_cast<DWORD*>(site+2),before,reinterpret_cast<DWORD>(&hudOrigin)};
    }
    if (valid) for (const auto& patch:patches) if (!Replace(patch.operand,patch.before,patch.after)) { valid=false; break; }
    if (!valid) {
        for (const auto& patch:patches) if (patch.operand) Replace(patch.operand,patch.after,patch.before);
        warned=true;
        Print("UltimateHL: renderer signature/patch validation failed; leaving the original renderer intact.\n");
        return;
    }
    installed=true;
    Print("UltimateHL: native NPOT uploads/safe mips; v3.1 nearest sprite style default, with corrected pixel-center/atlas-edge alignment.\n");
}
}

namespace Rendering {
void Initialize(void* functions) { std::memcpy(api.data(),functions,sizeof(api));Supersampling::Initialize(functions); }
void EnsureControls() {
    Supersampling::EnsureControls();
    static char n[]="uhl_native_textures", h[]="uhl_hud_texels", on[]="1";
    nativeTextures=Register(n,on);
    hudTexels=Register(h,on);spriteBlend=Cvar("gl_spriteblend");
    picmip=Cvar("gl_picmip"); roundDown=Cvar("gl_round_down"); maximum=Cvar("gl_max_size");
    TryInstall();
}
void Frame() {
    const bool pixelExact=hudTexels && hudTexels->value!=0,blended=spriteBlend && spriteBlend->value!=0;
    hudInset=HudTexelInset(pixelExact,blended);hudOrigin=HudPixelOriginShift(pixelExact,blended);
    TryInstall();Supersampling::Frame();
}
bool Ready() { return installed; }
unsigned NativeUploads() { return nativeUploads; }
unsigned SafeMips() { return safeMips; }
void Status() {
    char text[256]{};
    std::snprintf(text,sizeof(text),"Renderer: %s; avoided POT resizes=%u, safe NPOT mip steps=%u; HUD=%s, spriteblend=%.0f\n",
        installed ? "validated native-texel patch" : "original (patch unavailable)",nativeUploads,safeMips,
        hudTexels && hudTexels->value!=0 ? "v3.1 nearest, guarded pixel centers" : "stock blended",spriteBlend ? spriteBlend->value : -1);
    Print(text);
    Supersampling::Status();
}
void SelfTest() {
    if (!installed || !nativeTextures || nativeTextures->value==0 || !wglGetCurrentContext()) {
        Print("Renderer self-test unavailable: need the native texture path and current GL context.\n"); return;
    }
    constexpr int width=48,height=8;
    int legacyWidth=0,legacyHeight=0;
    originalDimensions(&legacyWidth,&legacyHeight,width,height);
    std::vector<unsigned char> source(width*height*4),readback(source.size());
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        const auto at=(y*width+x)*4;
        source[at]=0; source[at+1]=static_cast<unsigned char>(x%2 ? 0 : 255); source[at+2]=0; source[at+3]=255;
    }
    GLint previous=0; GLuint texture=0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    originalUpload(source.data(),width,height,1,0,GL_NEAREST);
    GLint actualWidth=0,actualHeight=0,lastWidth=0,lastHeight=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&actualWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&actualHeight);
    // Read only if dimensions match: never let an unexpected old 64-wide upload
    // overrun the deliberately 48-wide destination buffer.
    bool exact=false;
    if (actualWidth==width && actualHeight==height) {
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,readback.data());
        exact=readback==source;
    }
    glGetTexLevelParameteriv(GL_TEXTURE_2D,5,GL_TEXTURE_WIDTH,&lastWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,5,GL_TEXTURE_HEIGHT,&lastHeight);
    const GLenum error=glGetError();
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previous)); glDeleteTextures(1,&texture);
    char text[320]{};
    std::snprintf(text,sizeof(text),"Renderer GPU self-test: %s; legacy dimensions=%dx%d, native base=%dx%d, last mip=%dx%d, alternating green/black texels=%s, GL error=0x%x\n",
        exact && lastWidth==1 && lastHeight==1 && error==GL_NO_ERROR ? "PASS" : "FAIL",legacyWidth,legacyHeight,actualWidth,actualHeight,lastWidth,lastHeight,exact ? "exact" : "DIFFERENT",error);
    Print(text);
}
void Shutdown() {
    Supersampling::Shutdown();
    for (const auto& patch:patches) if (patch.operand) Replace(patch.operand,patch.after,patch.before);
    patches={}; installed=false;
}
}
