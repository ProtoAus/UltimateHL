#include "dxgi_bridge.hpp"
#include "display_presenter.hpp"
#include "engine_bridge.hpp"
#include "supersampling.hpp"
#include "presentation.hpp"
#include "diagnostics.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
namespace {
void* api[87]{};
using SwapFn=BOOL(WINAPI*)(HDC);
SwapFn original=nullptr;DWORD* slot=nullptr;bool installed=false,enabled=false,failed=false;
bool probe=false;unsigned frames=0,failures=0,cooldown=0,healthy=0;DxgiBridge bridge;
UhlCvar* ztrick=nullptr;float oldZ=0;bool changedZ=false;
template<class T>T Fn(unsigned i){return reinterpret_cast<T>(api[i]);}
void Print(const char* text){if(api[30])Fn<void(__cdecl*)(const char*)>(30)(text);}
bool Write(DWORD expected,DWORD value){
    if(!slot || !Diagnostics::Readable(slot,4) || *slot!=expected)return false;
    DWORD protection=0,ignored=0;if(!VirtualProtect(slot,4,PAGE_READWRITE,&protection))return false;
    *slot=value;VirtualProtect(slot,4,protection,&ignored);return true;
}
void Marker(int width,int height){
    if(!probe)return;
    glPushAttrib(GL_COLOR_BUFFER_BIT|GL_SCISSOR_BIT);glEnable(GL_SCISSOR_TEST);glColorMask(TRUE,TRUE,TRUE,TRUE);
    glScissor(0,height-32,32,32);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glScissor(width-32,0,32,32);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glPopAttrib();
}
BOOL WINAPI Swap(HDC dc){
    HWND parent=static_cast<HWND>(Presentation::GameWindow());
    if(!parent || WindowFromDC(dc)!=parent || failed)return original(dc);
    struct Screen {int size,width,height,flags,charHeight;short widths[256];}info{};info.size=sizeof(info);
    if(!api[12] || !Fn<int(__cdecl*)(Screen*)>(12)(&info))return original(dc);
    Marker(info.width,info.height);
    if(enabled){
        if(bridge.Ready() && (bridge.Context()!=wglGetCurrentContext() || bridge.Parent()!=parent || bridge.Width()!=info.width || bridge.Height()!=info.height)) {
            bridge.Reset();failed=true;Print("UltimateHL: GL output changed; legacy presentation restored until restart.\n");
            if(changedZ && ztrick){ztrick->value=oldZ;changedZ=false;}
            return original(dc);
        }
        if(cooldown){--cooldown;return original(dc);} // legacy frames after a failed copy
        if(!bridge.Ready()){
            RECT client{};GetClientRect(parent,&client);
            if(client.right!=info.width || client.bottom!=info.height){Print("UltimateHL: DXGI output/client dimensions differ; legacy presentation kept.\n");failed=true;return original(dc);}
            if(!bridge.Attach(parent,info.width,info.height)){
                Print("UltimateHL: DXGI presenter unavailable: ");Print(bridge.Error());Print("; legacy GL presentation kept.\n");failed=true;return original(dc);
            }
            if(api[72])ztrick=Fn<UhlCvar*(__cdecl*)(const char*)>(72)("gl_ztrick");
            if(ztrick){oldZ=ztrick->value;ztrick->value=0;changedZ=true;}
        }
        if(bridge.Present()) {++frames;if(++healthy>=300)failures=0;return TRUE;}
        // A failed copy is not fatal (a loading screen can span several frames): show legacy
        // GL for a while, then rebuild. Back off on repeats; give up after 5 failures in a
        // row without 300 good frames between them.
        healthy=0;const bool retry=++failures<5;
        Print("UltimateHL: DXGI presentation failed: ");Print(bridge.Error());
        Print(retry ? "; legacy GL briefly, then DXGI is rebuilt.\n" : "; returning to legacy GL.\n");
        failed=!retry;cooldown=retry ? 30u<<(failures-1) : 0;bridge.Reset();
        if(changedZ && ztrick){ztrick->value=oldZ;changedZ=false;}
    }
    return original(dc);
}
}
namespace DisplayPresenter {
void Initialize(void* functions){
    std::memcpy(api,functions,sizeof(api));wchar_t value[8]{};
    enabled=GetEnvironmentVariableW(L"UHL_DXGI_PRESENT",value,8) && value[0]==L'1';
    probe=GetEnvironmentVariableW(L"UHL_DISPLAY_PROBE",value,8) && value[0]==L'1';
}
void Frame(){
    if(installed || failed || (!enabled && !probe) || !EngineBridge::Supported() || !Supersampling::Ready() || !wglGetCurrentContext())return;
    auto* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));slot=reinterpret_cast<DWORD*>(base+0x1091bf8);
    const auto proc=GetProcAddress(GetModuleHandleW(L"opengl32.dll"),"wglSwapBuffers");
    auto* call=base+0x9ba966;DWORD address=0;
    bool valid=proc && Diagnostics::Readable(call,6) && call[0]==0xff && call[1]==0x15 && Diagnostics::Readable(slot,4);
    if(valid){std::memcpy(&address,call+2,4);valid=address==reinterpret_cast<DWORD>(slot) && *slot==reinterpret_cast<DWORD>(proc);}
    if(!valid){failed=true;Print("UltimateHL: swap-pointer validation failed; original presentation untouched.\n");return;}
    original=reinterpret_cast<SwapFn>(proc);
    installed=Write(reinterpret_cast<DWORD>(original),reinterpret_cast<DWORD>(Swap));
    if(!installed){failed=true;return;}
    Print(enabled ? "UltimateHL: full-resolution windowed DXGI output armed (GPU-only GL interop).\n" : "UltimateHL: legacy GL display probe armed.\n");
}
bool Capturing(){return bridge.Capturing();}
void Status(){
    char text[240]{};
    std::snprintf(text,sizeof(text),"Presentation: %s; DXGI frames=%u, output=%dx%d, probe=%d, game GL errors ignored=%u, copy failures in a row=%u. %s\n",
        bridge.Capturing() ? "windowed DXGI compositor" : (enabled ? "DXGI pending/unavailable" : "legacy OpenGL"),
        frames,bridge.Width(),bridge.Height(),probe,bridge.GameErrors(),failures,failed ? bridge.Error() : "");Print(text);
}
void DumpFrame(){
    if(!wglGetCurrentContext() || !api[12])return;
    struct Screen {int size,width,height,flags,charHeight;short widths[256];}info{};info.size=sizeof(info);
    if(!Fn<int(__cdecl*)(Screen*)>(12)(&info) || info.width<1 || info.height<1 || info.width>8192 || info.height>8192)return;
    wchar_t token[64]{},module[MAX_PATH]{};const DWORD length=GetEnvironmentVariableW(L"UHL_LAUNCH_TOKEN",token,64);
    if(!length || length>=64)return;
    for(DWORD i=0;i<length;++i)if(token[i]!=L'-' && (token[i]<L'0' || token[i]>L'9'))return;
    GetModuleFileNameW(nullptr,module,MAX_PATH);
    const auto path=std::filesystem::path(module).parent_path()/(L"uhl-frame-"+std::wstring(token)+L".bmp");
    GLint packBuffer=0;glGetIntegerv(0x88ed,&packBuffer);if(packBuffer)return;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(info.width)*info.height*4);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    glReadPixels(0,0,info.width,info.height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());glPopClientAttrib();
    if(glGetError()!=GL_NO_ERROR){Print("Frame dump failed: GL read error.\n");return;}
    std::vector<unsigned char> bgra(rgba.size());
    for(int y=0;y<info.height;++y)for(int x=0;x<info.width;++x){
        const auto src=(static_cast<std::size_t>(info.height-1-y)*info.width+x)*4,dst=(static_cast<std::size_t>(y)*info.width+x)*4;
        bgra[dst]=rgba[src+2];bgra[dst+1]=rgba[src+1];bgra[dst+2]=rgba[src];bgra[dst+3]=255;
    }
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return;
    BITMAPFILEHEADER head{};BITMAPINFOHEADER bitmap{};bitmap.biSize=sizeof(bitmap);bitmap.biWidth=info.width;bitmap.biHeight=-info.height;
    bitmap.biPlanes=1;bitmap.biBitCount=32;bitmap.biCompression=BI_RGB;head.bfType=0x4d42;head.bfOffBits=sizeof(head)+sizeof(bitmap);
    head.bfSize=head.bfOffBits+static_cast<DWORD>(bgra.size());DWORD written=0;
    const bool ok=WriteFile(file,&head,sizeof(head),&written,nullptr) && WriteFile(file,&bitmap,sizeof(bitmap),&written,nullptr) &&
        WriteFile(file,bgra.data(),static_cast<DWORD>(bgra.size()),&written,nullptr);CloseHandle(file);
    Print(ok ? "Native game-only framebuffer BMP saved (not a desktop screenshot).\n" : "Frame dump write failed.\n");
}
void Shutdown(){
    bridge.Reset();if(changedZ && ztrick)ztrick->value=oldZ;changedZ=false;
    if(installed)Write(reinterpret_cast<DWORD>(Swap),reinterpret_cast<DWORD>(original));installed=false;
}
}
