#include "scene_target.hpp"
#include "hud_scaling.hpp"
#include "hud_split.hpp"
#include "hud_blend.hpp"
#include "engine_bridge.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
namespace {
struct Screen {int size,width,height,flags,charHeight;short widths[256];};
static_assert(sizeof(Screen)==532);
struct WRect {int left,right,top,bottom;};
struct ClientSprite {char name[64],sprite[64];int handle,res;WRect rc;};
static_assert(sizeof(ClientSprite)==152);
// Split scale: only health, armor and ammo (and BXT's timer and speedometer, which BXT sorts
// itself via UHL_HudScalePass) are enlarged; text, the crosshair and everything else stay 1x.
// The HUD is drawn twice per frame (the client's time step is 0 on the second call): the
// enlarged elements on the canvas, then the rest natively. The client's draws are sorted by
// the scaled elements' exact sprite rectangles from hud.txt and the weapon lists; a FillRGBA
// right after one of them on the same line is its bar.
enum HudPass {Whole,Scaled,Native};
HudPass pass=Whole;
HudSorter sorter;int rectsRes=0;
using SetFn=void(__cdecl*)(int,int,int,int);using DrawFn=void(__cdecl*)(int,int,int,const WRect*);
using FillFn=void(__cdecl*)(int,int,int,int,int,int,int,int);using CharFn=int(__cdecl*)(int,int,int,int,int,int);
using StringFn=int(__cdecl*)(int,int,char*);using StringLenFn=void(__cdecl*)(const char*,int*,int*);
SetFn originalSet=nullptr;std::array<DrawFn,3> originalDraws{};FillFn originalFill=nullptr;
CharFn originalChar=nullptr;StringFn originalString=nullptr;bool splitReady=false;int bxtVersion=0;
std::array<void*,87> api{};void** engineTable=nullptr;UhlCvar* scale=nullptr;
using InfoFn=int(__cdecl*)(Screen*);using RedrawFn=int(__cdecl*)(float,int);
using BlendFn=void(APIENTRY*)(GLenum,GLenum);
using SeparateFn=void(APIENTRY*)(GLenum,GLenum,GLenum,GLenum);
InfoFn originalInfo=nullptr;RedrawFn originalRedraw=nullptr;BlendFn originalBlend=nullptr;SeparateFn separate=nullptr;
SceneTarget canvas;Screen *cached=nullptr,*bxtCached=nullptr;
bool bxtChecked=false,bxtAware=false;HMODULE knownBxt=nullptr;
bool installed=false,attempted=false,active=false,warned=false;
int physicalWidth=0,physicalHeight=0,virtualWidth=0,virtualHeight=0;
unsigned frames=0,failures=0;
struct Patch {void** slot=nullptr;void* before=nullptr;void* after=nullptr;};
std::array<Patch,6> patches{};std::array<Patch,7> drawPatches{};Patch bxtBlend{};
template<class T>T Fn(unsigned i){return reinterpret_cast<T>(api[i]);}
void Print(const char* s){if(api[30])Fn<void(__cdecl*)(const char*)>(30)(s);}
bool Store(Patch& p,bool undo){
    if(!p.slot || !Diagnostics::Readable(p.slot,4) || *p.slot!=(undo ? p.after : p.before))return false;
    DWORD old=0,ignored=0;if(!VirtualProtect(p.slot,4,PAGE_READWRITE,&old))return false;
    *p.slot=undo ? p.before : p.after;VirtualProtect(p.slot,4,old,&ignored);return true;
}
int __cdecl GetInfo(Screen* info){
    const int result=originalInfo(info);
    if(result && active && info){info->width=virtualWidth;info->height=virtualHeight;}
    return result;
}
void APIENTRY Blend(GLenum source,GLenum destination){
    if(!active){originalBlend(source,destination);return;}
    const auto alpha=HudCoverage(source,destination);separate(source,destination,alpha.source,alpha.destination);
}
void BuildScaledRects(int res){
    sorter.scaled.clear();rectsRes=res;
    const auto add=[res](const std::string& list,std::initializer_list<const char*> names){
        int count=0;auto* sprites=Fn<ClientSprite*(__cdecl*)(char*,int*)>(10)(const_cast<char*>(list.c_str()),&count);
        for(int i=0;sprites && i<count;++i){
            if(sprites[i].res!=res)continue;
            for(const char* name:names)if(!std::strcmp(sprites[i].name,name)){
                const std::string path="sprites/"+std::string(sprites[i].sprite)+".spr";
                const WRect& rc=sprites[i].rc;
                sorter.scaled.push_back({Fn<int(__cdecl*)(const char*)>(0)(path.c_str()),{rc.left,rc.right,rc.top,rc.bottom}});break;
            }
        }
    };
    add("sprites/hud.txt",{"number_0","number_1","number_2","number_3","number_4","number_5","number_6","number_7",
        "number_8","number_9","divider","cross","suit_full","suit_empty"});
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);if(auto* slash=wcsrchr(exe,L'\\'))slash[1]=0;
    std::vector<std::string> weapons;
    for(const wchar_t* folder:{L"valve_WON\\sprites\\",L"valve\\sprites\\"}){
        WIN32_FIND_DATAW entry{};HANDLE find=FindFirstFileW((std::wstring(exe)+folder+L"weapon_*.txt").c_str(),&entry);
        if(find==INVALID_HANDLE_VALUE)continue;
        do{std::string name;for(const wchar_t* c=entry.cFileName;*c;++c)name+=static_cast<char>(*c);
            if(std::find(weapons.begin(),weapons.end(),name)==weapons.end())weapons.push_back(name);}while(FindNextFileW(find,&entry));
        FindClose(find);
    }
    for(const auto& weapon:weapons)add("sprites/"+weapon,{"ammo","ammo2"});
}
bool Skip(bool scaled){return (pass==Scaled && !scaled) || (pass==Native && scaled);}
void __cdecl SprSet(int sprite,int r,int g,int b){sorter.Set(sprite);originalSet(sprite,r,g,b);}
template<int I> void __cdecl SprDraw(int frame,int x,int y,const WRect* rc){
    if(pass!=Whole && Skip(sorter.Sprite(reinterpret_cast<const HudRect*>(rc),y)))return;
    originalDraws[I](frame,x,y,rc);
}
void __cdecl FillRgba(int x,int y,int w,int h,int r,int g,int b,int a){
    if(pass!=Whole && Skip(sorter.Fill(y,h)))return;
    originalFill(x,y,w,h,r,g,b,a);
}
int __cdecl DrawCharacter(int x,int y,int ch,int r,int g,int b){
    sorter.Text();
    if(pass==Scaled)return cached ? cached->widths[ch&255] : 0; // text is drawn in the 1x pass
    return originalChar(x,y,ch,r,g,b);
}
int __cdecl DrawString(int x,int y,char* text){
    sorter.Text();
    if(pass==Scaled){int w=0,h=0;if(api[29])Fn<StringLenFn>(29)(text,&w,&h);return x+w;}
    return originalString(x,y,text);
}
int __cdecl Redraw(float time,int intermission){
    const float wanted=scale ? scale->value : 1;
    if(active || !ValidHudScale(wanted))return originalRedraw(time,intermission);
    Screen info{};info.size=sizeof(info);
    if(!originalInfo(&info) || !cached || cached->size!=static_cast<int>(sizeof(Screen)) || cached->width!=info.width || cached->height!=info.height)
        return originalRedraw(time,intermission);
    const auto bxt=GetModuleHandleW(L"BunnymodXT.dll");
    // A canvas-aware BXT reads the (scoped) screen size itself; the official DLL's cache is scoped here.
    Screen* bxtScreen=bxt && !bxtAware ? bxtCached : nullptr;
    const bool bxtCacheValid=bxtAware || (bxtScreen && Diagnostics::Readable(bxtScreen,sizeof(Screen)) &&
        bxtScreen->size==static_cast<int>(sizeof(Screen)) && bxtScreen->width==info.width && bxtScreen->height==info.height);
    if(bxt && (bxt!=knownBxt || !bxtBlend.slot || !bxtCacheValid)){
        ++failures;if(!warned){Print("UltimateHL: BXT HUD cache not validated; native HUD size preserved.\n");warned=true;}
        return originalRedraw(time,intermission);
    }
    if(!canvas.BeginHud(info.width,info.height,wanted)){
        ++failures;if(!warned){Print("UltimateHL: HUD canvas unavailable; native HUD preserved. ");Print(canvas.Error());Print("\n");warned=true;}
        return originalRedraw(time,intermission);
    }
    physicalWidth=info.width;physicalHeight=info.height;virtualWidth=canvas.Width();virtualHeight=canvas.Height();
    const int oldWidth=cached->width,oldHeight=cached->height;cached->width=virtualWidth;cached->height=virtualHeight;
    // BXT caches SCREENINFO at VidInit, not on each draw. Scope its dimensions
    // too, without replaying VidInit or reloading sprites every frame.
    if(bxtScreen){bxtScreen->width=virtualWidth;bxtScreen->height=virtualHeight;}
    // The official BXT cannot sort its own HUD, so with it the whole HUD is enlarged.
    const bool split=splitReady && (!bxt || bxtVersion>=2);
    if(split){const int res=info.width<640 ? 320 : 640;if(rectsRes!=res)BuildScaledRects(res);pass=Scaled;sorter.Begin();}
    GLint mode=GL_MODELVIEW;glGetIntegerv(GL_MATRIX_MODE,&mode);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,virtualWidth,virtualHeight,0,-99999,99999);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();active=true;
    int result=originalRedraw(time,intermission); // includes BXT's HUD
    active=false;cached->width=oldWidth;cached->height=oldHeight;
    if(bxtScreen){bxtScreen->width=oldWidth;bxtScreen->height=oldHeight;}
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(static_cast<GLenum>(mode));
    canvas.End();++frames;
    if(split){pass=Native;sorter.Begin();result=originalRedraw(time,intermission);pass=Whole;} // the rest at 1x, on top
    return result;
}
void Install(){
    if(attempted || !EngineBridge::Supported() || !wglGetCurrentContext())return;
    auto* client=reinterpret_cast<BYTE*>(GetModuleHandleW(L"client.dll"));if(!client)return;
    attempted=true;auto* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    originalRedraw=reinterpret_cast<RedrawFn>(GetProcAddress(reinterpret_cast<HMODULE>(client),"HUD_Redraw"));
    originalInfo=reinterpret_cast<InfoFn>(api[12]);
    const auto gl=GetModuleHandleW(L"opengl32.dll");originalBlend=reinterpret_cast<BlendFn>(GetProcAddress(gl,"glBlendFunc"));
    const auto proc=wglGetProcAddress("glBlendFuncSeparate");const auto n=reinterpret_cast<std::intptr_t>(proc);
    separate=proc && n!=1 && n!=2 && n!=3 && n!=-1 ? reinterpret_cast<SeparateFn>(proc) : nullptr;
    cached=reinterpret_cast<Screen*>(client+0x66e50);
    auto** redrawSlot=reinterpret_cast<void**>(base+0x1b712ac);
    auto** clientInfo=reinterpret_cast<void**>(client+0x67078+12*4);
    bool valid=originalRedraw==reinterpret_cast<RedrawFn>(client+0xac60) && originalInfo && originalBlend && separate &&
        Diagnostics::Readable(redrawSlot-1,12) && redrawSlot[-1]==client+0xac20 && *redrawSlot==reinterpret_cast<void*>(originalRedraw) &&
        redrawSlot[1]==client+0xac80 && Diagnostics::Readable(cached,sizeof(Screen)) &&
        Diagnostics::Readable(clientInfo,4) && *clientInfo==reinterpret_cast<void*>(originalInfo) &&
        engineTable && Diagnostics::Readable(engineTable+12,4) && engineTable[12]==reinterpret_cast<void*>(originalInfo);
    if(valid){
        patches[0]={redrawSlot,reinterpret_cast<void*>(originalRedraw),reinterpret_cast<void*>(Redraw)};
        patches[1]={clientInfo,reinterpret_cast<void*>(originalInfo),reinterpret_cast<void*>(GetInfo)};
        if(engineTable+12!=clientInfo)patches[2]={engineTable+12,reinterpret_cast<void*>(originalInfo),reinterpret_cast<void*>(GetInfo)};
        const DWORD blendSlots[]{0x13e83f4,0x13e85dc};
        for(unsigned i=0;i<2;++i){
            auto** slot=reinterpret_cast<void**>(base+blendSlots[i]);
            valid=valid && Diagnostics::Readable(slot,4) && *slot==reinterpret_cast<void*>(originalBlend);
            patches[i+3]={slot,reinterpret_cast<void*>(originalBlend),reinterpret_cast<void*>(Blend)};
        }
    }
    if(valid)for(auto& p:patches)if(p.slot && !Store(p,false)){valid=false;break;}
    if(!valid){for(auto& p:patches)if(p.slot && *p.slot==p.after)Store(p,true);Print("UltimateHL: HUD scale validation failed; native HUD left intact.\n");return;}
    installed=true;Print("UltimateHL: independent 1x/1.25x/1.5x/1.75x/2x HUD canvas ready (premultiplied bilinear filtering).\n");
    // Split scale sorts the client's own draw calls: its copy of the engine table, entry by
    // entry, only while every entry is still the engine's.
    auto** clientTable=reinterpret_cast<void**>(client+0x67078);
    const unsigned slots[7]{4,5,6,7,11,26,27}; // SPR_Set, SPR_Draw, SPR_DrawHoles, SPR_DrawAdditive, FillRGBA, DrawCharacter, DrawConsoleString
    void* const hooks[7]{reinterpret_cast<void*>(SprSet),reinterpret_cast<void*>(&SprDraw<0>),reinterpret_cast<void*>(&SprDraw<1>),
        reinterpret_cast<void*>(&SprDraw<2>),reinterpret_cast<void*>(FillRgba),reinterpret_cast<void*>(DrawCharacter),reinterpret_cast<void*>(DrawString)};
    splitReady=Diagnostics::Readable(clientTable,28*sizeof(void*)) && Diagnostics::Readable(engineTable,28*sizeof(void*));
    for(unsigned i=0;i<7 && splitReady;++i){
        splitReady=clientTable[slots[i]]==engineTable[slots[i]];
        drawPatches[i]={clientTable+slots[i],clientTable[slots[i]],hooks[i]};
    }
    if(splitReady){
        originalSet=reinterpret_cast<SetFn>(drawPatches[0].before);
        for(unsigned i=0;i<3;++i)originalDraws[i]=reinterpret_cast<DrawFn>(drawPatches[i+1].before);
        originalFill=reinterpret_cast<FillFn>(drawPatches[4].before);
        originalChar=reinterpret_cast<CharFn>(drawPatches[5].before);originalString=reinterpret_cast<StringFn>(drawPatches[6].before);
        for(auto& p:drawPatches)if(!Store(p,false)){splitReady=false;break;}
    }
    if(!splitReady){for(auto& p:drawPatches)if(p.slot && *p.slot==p.after)Store(p,true);Print("UltimateHL: split HUD scale unavailable; the whole HUD is enlarged.\n");}
}
void HookBxtBlend(){
    if(!installed || bxtChecked)return;
    auto* module=reinterpret_cast<BYTE*>(GetModuleHandleW(L"BunnymodXT.dll"));if(!module)return;
    bxtChecked=true;
    // A rebuilt BXT that exports BXT_HudCanvasVersion takes the screen size per HUD draw;
    // only the supplied official DLL needs its cached size located at known addresses.
    // Version 2 also draws its timer/speedometer and everything else in separate passes.
    const auto version=reinterpret_cast<int(__cdecl*)()>(GetProcAddress(reinterpret_cast<HMODULE>(module),"BXT_HudCanvasVersion"));
    bxtVersion=version ? version() : 0;bxtAware=bxtVersion>=1;
    const bool official=!bxtAware && EngineBridge::SupportedBxt(module);
    if(!bxtAware && !official)return;
    knownBxt=reinterpret_cast<HMODULE>(module);
    auto* screen=reinterpret_cast<Screen*>(module+0x65f898);
    const BYTE setSize[]{0xc7,0x05},getScreen[]{0x8b,0x40,0x30,0xff,0xd0};
    DWORD pointer=0,size=0,pushed=0;
    if(official && Diagnostics::Readable(module+0x13db2a,10) && Diagnostics::Readable(module+0x13db8f,10)){
        std::memcpy(&pointer,module+0x13db2c,4);std::memcpy(&size,module+0x13db30,4);std::memcpy(&pushed,module+0x13db90,4);
        if(!std::memcmp(module+0x13db2a,setSize,2) && pointer==reinterpret_cast<DWORD>(screen) && size==sizeof(Screen) &&
           module[0x13db8f]==0x68 && pushed==pointer && !std::memcmp(module+0x13db94,getScreen,5) && Diagnostics::Readable(screen,sizeof(Screen)))bxtCached=screen;
    }
    const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);if(!Diagnostics::Readable(dos,sizeof(*dos)) || dos->e_magic!=IMAGE_DOS_SIGNATURE)return;
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(module+dos->e_lfanew);
    if(!Diagnostics::Readable(nt,sizeof(*nt)) || nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386)return;
    const auto imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!imports.VirtualAddress || !Diagnostics::Readable(module+imports.VirtualAddress,imports.Size))return;
    const auto* desc=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module+imports.VirtualAddress);
    for(unsigned d=0;d<imports.Size/sizeof(*desc) && desc[d].Name;++d){
        if(!desc[d].OriginalFirstThunk)continue;
        const auto* names=reinterpret_cast<IMAGE_THUNK_DATA*>(module+desc[d].OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA*>(module+desc[d].FirstThunk);
        for(unsigned i=0;i<4096 && Diagnostics::Readable(names+i,sizeof(*names)) && names[i].u1.AddressOfData;++i){
            if(IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))continue;
            const auto* entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(module+names[i].u1.AddressOfData);
            if(!Diagnostics::Readable(entry,16) || std::strcmp(reinterpret_cast<const char*>(entry->Name),"glBlendFunc"))continue;
            bxtBlend={reinterpret_cast<void**>(&slots[i].u1.Function),reinterpret_cast<void*>(originalBlend),reinterpret_cast<void*>(Blend)};
            if(!Store(bxtBlend,false))bxtBlend={};return;
        }
    }
}
}
namespace HudScaling {
void EnsureControls(){
    if(!api[72] || !api[14])return;
    scale=Fn<UhlCvar*(__cdecl*)(const char*)>(72)("uhl_hud_scale");
    if(!scale){static char name[]="uhl_hud_scale",one[]="1";scale=Fn<UhlCvar*(__cdecl*)(char*,char*,int)>(14)(name,one,1);}
}
void Initialize(void* functions){std::memcpy(api.data(),functions,sizeof(api));engineTable=static_cast<void**>(functions);EnsureControls();}
void Frame(){Install();HookBxtBlend();}
bool Active(){return active;}
int Pass(){return pass;}
int HeightDelta(){return active ? physicalHeight-virtualHeight : 0;}
int Width(){return virtualWidth;}int Height(){return virtualHeight;}
void Status(){char text[240]{};std::snprintf(text,sizeof(text),"HUD canvas: %s, requested=%gx, composed=%u, fallback=%u, virtual=%dx%d -> %dx%d; bilinear premultiplied, BXT blend/cache=%s/%s.\n",
    installed ? "ready" : "unavailable",scale ? scale->value : 1,frames,failures,virtualWidth,virtualHeight,physicalWidth,physicalHeight,bxtBlend.slot ? "scoped" : "pending/not loaded",
    bxtAware ? "BXT reads it" : (bxtCached ? "scoped" : "none"));Print(text);
    const bool split=splitReady && (!GetModuleHandleW(L"BunnymodXT.dll") || bxtVersion>=2);
    std::snprintf(text,sizeof(text),"HUD scale mode: %s; %u sprite rectangles scaled.\n",
        split ? "split (health, armor, ammo and BXT timer/speedometer enlarged; text, crosshair and the rest 1x)" : "whole HUD",
        static_cast<unsigned>(sorter.scaled.size()));Print(text);}
void Shutdown(){canvas.Reset();if(bxtBlend.slot)Store(bxtBlend,true);bxtBlend={};for(auto& p:patches)if(p.slot && *p.slot==p.after)Store(p,true);
    for(auto& p:drawPatches)if(p.slot && *p.slot==p.after)Store(p,true);drawPatches={};splitReady=false;installed=false;}
}
