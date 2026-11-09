#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <cstdio>
#include "practice_cheats.hpp"
#include "cheat_policy.hpp"
#include "engine_bridge.hpp"
#include "diagnostics.hpp"
namespace {
void* api[87]{};UhlCvar *control=nullptr,*cheats=nullptr;
int *active=nullptr,*maxClients=nullptr,*latch=nullptr;float *deathmatch=nullptr,*coop=nullptr;
bool ready=false,failed=false;
BYTE* serverModule=nullptr;float* weaponCheats=nullptr;
using Handler=void(__cdecl*)();
struct Wrapped {const char* name;DWORD rva;Handler original;bool installed;};
Wrapped commands[]={{"god",0x959ea0,nullptr,false},{"noclip",0x959fe0,nullptr,false},{"impulse",0x12f90,nullptr,false}};
template<class T>T Fn(unsigned i){return reinterpret_cast<T>(api[i]);}
void Print(const char* text){if(api[30])Fn<void(__cdecl*)(const char*)>(30)(text);}
bool Operand(const BYTE* site,const BYTE* opcode,unsigned length,DWORD expected){
    DWORD value=0;if(!Diagnostics::Readable(site,length+4) || std::memcmp(site,opcode,length))return false;
    std::memcpy(&value,site+length,4);return value==expected;
}
void FindServer(){
    auto* module=reinterpret_cast<BYTE*>(GetModuleHandleW(L"hl.dll"));
    if(module==serverModule)return;serverModule=module;weaponCheats=nullptr;
    if(!module || !EngineBridge::SupportedServer(module))return;
    const BYTE push[]={0x68},call[]={0xff,0x15},store[]={0xd9,0x1d},load[]={0xd9,0x05};
    if(Operand(module+0x90bff,push,1,reinterpret_cast<DWORD>(module+0xd33bc)) &&
       Operand(module+0x90c04,call,2,reinterpret_cast<DWORD>(module+0xd75a4)) &&
       Operand(module+0x90c0a,store,2,reinterpret_cast<DWORD>(module+0xe77f0)) &&
       Operand(module+0x14fea,load,2,reinterpret_cast<DWORD>(module+0xe77f0)) && Diagnostics::Readable(module+0xe77f0,4))
        weaponCheats=reinterpret_cast<float*>(module+0xe77f0);
}
void Sync(){
    FindServer();
    if(!ready || !cheats || !control || !MaySyncCheats(control->value!=0,*active,*maxClients,*deathmatch,*coop,cheats->value))return;
    const int wanted=cheats->value!=0 ? 1 : 0;
    if(*latch!=wanted){*latch=wanted;Diagnostics::Event("practice_cheats_latch",wanted);}
    // Stock WON GameDLL separately caches sv_cheats for CheatImpulseCommands.
    // The engine latch alone fixes god/noclip, but cannot unlock impulse 101.
    if(weaponCheats && *weaponCheats!=static_cast<float>(wanted)){
        *weaponCheats=static_cast<float>(wanted);Diagnostics::Event("practice_weapon_cheats",wanted);
    }
}
void God(){Sync();commands[0].original();}
void Noclip(){Sync();commands[1].original();}
void Impulse(){Sync();commands[2].original();}
Handler replacements[]={God,Noclip,Impulse};
void Install(){
    if(ready || failed || !EngineBridge::Supported())return;
    auto* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    const BYTE a1[]={0xa1},fld[]={0xd9,0x05},compare[]={0x39,0x0d};
    // Host_ValidSave exposes sv.active and svs.maxclients. Independent god and
    // noclip checks must identify the SAME server cheat latch. No branch NOPs.
    bool valid=Operand(base+0x95ad62,a1,1,reinterpret_cast<DWORD>(base+0x1118840)) &&
        Operand(base+0x95ad7b,compare,2,reinterpret_cast<DWORD>(base+0x1117c68)) &&
        Operand(base+0x959ec1,a1,1,reinterpret_cast<DWORD>(base+0x1115a20)) &&
        Operand(base+0x95a010,a1,1,reinterpret_cast<DWORD>(base+0x1115a20)) &&
        Operand(base+0x959eae,fld,2,reinterpret_cast<DWORD>(base+0x1117d74));
    active=reinterpret_cast<int*>(base+0x1118840);maxClients=reinterpret_cast<int*>(base+0x1117c68);
    latch=reinterpret_cast<int*>(base+0x1115a20);deathmatch=reinterpret_cast<float*>(base+0x1117d74);coop=deathmatch+1;
    valid=valid && Diagnostics::Readable(active,4) && Diagnostics::Readable(maxClients,4) && Diagnostics::Readable(latch,4) && Diagnostics::Readable(deathmatch,8);
    cheats=api[72] ? Fn<UhlCvar*(__cdecl*)(const char*)>(72)("sv_cheats") : nullptr;
    if(!valid || !cheats){failed=true;Print("UltimateHL: immediate-cheats validation failed; stock checks preserved.\n");return;}
    ready=true;
    Print("UltimateHL: immediate sv_cheats latch ready (local single-player only; never enables cheats by itself).\n");
}
void WrapAvailable(){
    if(!ready)return;
    for(unsigned i=0;i<3;++i){
        auto& cmd=commands[i];if(cmd.installed)continue;
        auto* module=reinterpret_cast<BYTE*>(GetModuleHandleW(i==2 ? L"client.dll" : nullptr));
        void* original=EngineBridge::CommandHandler(cmd.name);
        if(!module || original!=module+cmd.rva)continue;
        cmd.original=reinterpret_cast<Handler>(original);
        cmd.installed=EngineBridge::ReplaceCommand(cmd.name,original,reinterpret_cast<void*>(replacements[i]));
    }
}
}
namespace PracticeCheats {
void Initialize(void* functions){std::memcpy(api,functions,sizeof(api));Frame();}
void Frame(){
    if(!control && api[72] && api[14]){
        control=Fn<UhlCvar*(__cdecl*)(const char*)>(72)("uhl_cheats_live");
        if(!control){static char name[]="uhl_cheats_live",on[]="1";control=Fn<UhlCvar*(__cdecl*)(char*,char*,int)>(14)(name,on,1);}
    }
    Install();WrapAvailable();Sync();
}
void Status(){
    Sync();unsigned weapons=0;
    if(weaponCheats && ready && *active==1 && *maxClients==1){
        using EntityFn=BYTE*(__cdecl*)(int);auto* slot=serverModule+0xd74c0+72*4;
        if(Diagnostics::Readable(slot,4)){
            EntityFn entity=nullptr;std::memcpy(&entity,slot,4);auto* player=entity ? entity(1) : nullptr;
            if(Diagnostics::Readable(player,0x1ec))std::memcpy(&weapons,player+0x1e8,4);
        }
    }
    char text[300]{};std::snprintf(text,sizeof(text),"Practice cheats: %s, control=%.0f, local active=%d, maxclients=%d, requested=%.0f, server latch=%d, GameDLL impulse latch=%.0f, weapons=%08x suit=%d. No auto-enable.\n",
        ready ? "validated" : (failed ? "stock fallback" : "pending"),control ? control->value : 0,
        ready ? *active : 0,ready ? *maxClients : 0,cheats ? cheats->value : 0,ready ? *latch : 0,
        weaponCheats ? *weaponCheats : -1,weapons,(weapons&0x80000000u)!=0);Print(text);
}
void Shutdown(){
    for(unsigned i=0;i<3;++i)if(commands[i].installed){
        EngineBridge::ReplaceCommand(commands[i].name,reinterpret_cast<void*>(replacements[i]),reinterpret_cast<void*>(commands[i].original));commands[i].installed=false;
    }
    // Do not silently revoke a user's intentional cheat choice on shutdown.
    ready=false;
}
}
