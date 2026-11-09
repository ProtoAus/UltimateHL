#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "brightness.hpp"
#include "brightness_pass.hpp"
#include "diagnostics.hpp"
namespace {
void* api[87]{};
UhlCvar *amount=nullptr,*lock=nullptr;
BrightnessPass pass;
bool warned=false;unsigned passes=0,failures=0,resets=0,redirects=0;
ULONGLONG started=0;
// The engine's gamma family rebuilds lightmaps and texture gamma instead of adjusting the
// picture. While uhl_gamma_lock is on they stay at the engine's defaults, as every
// untouched config.cfg records them (lightgamma/texgamma are never archived).
struct Legacy {const char* name;float value;UhlCvar* var;};
Legacy legacy[]={{"gamma",2.5f,nullptr},{"brightness",1.0f,nullptr},{"lightgamma",2.5f,nullptr},{"texgamma",2.0f,nullptr}};
// Startup configs may still hold old values; only later changes are a request to brighten.
constexpr ULONGLONG StartupGrace=5000;
// While locked, the engine's brightness holds this text: exactly the default value (same
// lightmaps), but not what anyone types, so every later write is seen, even "brightness 1".
constexpr char BrightnessMarker[]="1.0000";
unsigned markerWait=0; // frames to wait for our queued reset before acting on a mismatch again
template<class T>T Fn(unsigned i){return reinterpret_cast<T>(api[i]);}
void Print(const char* s){if(api[30])Fn<void(__cdecl*)(const char*)>(30)(s);}
UhlCvar* Find(const char* name){return api[72] ? Fn<UhlCvar*(__cdecl*)(const char*)>(72)(name) : nullptr;}
void Set(const char* name,float value){if(api[37])Fn<void(__cdecl*)(char*,float)>(37)(const_cast<char*>(name),value);}
void Command(const char* text){if(api[20])Fn<int(__cdecl*)(char*)>(20)(const_cast<char*>(text));}
UhlCvar* Register(char* name,char* value){
    auto* var=Find(name);
    return var || !api[14] ? var : Fn<UhlCvar*(__cdecl*)(char*,char*,int)>(14)(name,value,1);
}
}
namespace Brightness {
void Initialize(void* functions){std::memcpy(api,functions,sizeof(api));started=GetTickCount64();EnsureControls();}
void EnsureControls(){
    static char amountName[]="uhl_brightness",zero[]="0",lockName[]="uhl_gamma_lock",one[]="1";
    if(!amount)amount=Register(amountName,zero);
    if(!lock)lock=Register(lockName,one);
}
void Frame(){
    if(!lock || lock->value==0)return;
    const bool startup=GetTickCount64()-started<StartupGrace;
    if(markerWait)--markerWait; // retry if the reset never ran
    for(auto& l:legacy){
        if(!l.var)l.var=Find(l.name);
        if(!l.var)continue;
        if(!std::strcmp(l.name,"brightness")){
            if(l.var->string && !std::strcmp(l.var->string,BrightnessMarker)){markerWait=0;continue;}
            if(markerWait)continue; // our queued reset has not run yet
            if(!startup && amount){Set("uhl_brightness",std::clamp(l.var->value-l.value,-0.9f,3.0f));++redirects;}
            Command("brightness 1.0000\n"); // runs before the next frame
            markerWait=30;++resets;continue;
        }
        if(l.var->value==l.value)continue;
        Set(l.name,l.value);++resets;
    }
}
void AfterScene(){
    const float wanted=amount ? amount->value : 0;
    if(wanted==0 || !api[12])return;
    struct Screen {int size,width,height,flags,charHeight;short widths[256];}info{};info.size=sizeof(info);
    if(!Fn<int(__cdecl*)(Screen*)>(12)(&info))return;
    if(pass.Apply(info.width,info.height,wanted)){++passes;return;}
    ++failures;
    if(!warned){Print("UltimateHL: brightness pass unavailable: ");Print(pass.Error());Print("\n");warned=true;}
}
void Status(){
    char text[300]{};const float b=amount ? amount->value : 0;
    std::snprintf(text,sizeof(text),"Brightness: uhl_brightness=%g (picture curve exponent %.3f, 3D view only), passes=%u, failures=%u; "
        "legacy gamma lock=%s (gamma 2.5, brightness 1, lightgamma 2.5, texgamma 2), resets=%u, brightness redirects=%u.\n",
        b,BrightnessPass::Exponent(b),passes,failures,lock && lock->value!=0 ? "on" : "off",resets,redirects);
    Print(text);
}
void Shutdown(){pass.Reset();amount=lock=nullptr;for(auto& l:legacy)l.var=nullptr;}
}
