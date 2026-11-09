// Attaches the adapter to the real stock client.dll in a plain process (no engine): the nine
// adapted exports must resolve to stubs inside the client image that reach UHL_*, and nothing
// else may change. Skips (77) when the stock client is not on this machine.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
static void Check(bool value,const char* label){if(!value){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}}
int main(int argc,char** argv){
    if(argc<3 || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES){std::cout<<"SKIP: stock client.dll not found\n";return 77;}
    // client.dll imports vgui.dll from the Half-Life folder, two levels above cl_dlls.
    std::string folder=argv[1];
    for(int up=0;up<3;++up)folder=folder.substr(0,folder.find_last_of("\\/"));
    SetDllDirectoryA(folder.c_str());
    HMODULE client=LoadLibraryA(argv[1]);Check(client!=nullptr,"stock client loads without the engine");
    const char* names[9]={"Initialize","IN_Accumulate","CL_CreateMove","HUD_Frame","HUD_Shutdown","IN_ClearStates","HUD_Key_Event","IN_MouseEvent","HUD_Init"};
    const char* adapters[9]={"UHL_Initialize","UHL_Accumulate","UHL_CreateMove","UHL_Frame","UHL_Shutdown","UHL_ClearStates","UHL_KeyEvent","UHL_MouseEvent","UHL_HudInit"};
    FARPROC before[9]{};for(int i=0;i<9;++i)before[i]=GetProcAddress(client,names[i]);
    const FARPROC redrawBefore=GetProcAddress(client,"HUD_Redraw");
    HMODULE uhl=LoadLibraryA(argv[2]);Check(uhl!=nullptr,"adapter loads");
    auto attach=reinterpret_cast<int(__cdecl*)(HMODULE)>(GetProcAddress(uhl,"UHL_AttachClient"));Check(attach!=nullptr,"UHL_AttachClient exported");
    Check(attach(GetModuleHandleW(nullptr))==0,"refuses a module that is not the stock client");
    Check(attach(client)==1,"attaches to the stock client");
    Check(attach(client)==0,"attaches only once");
    auto* base=reinterpret_cast<BYTE*>(client);
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const SIZE_T imageSize=nt->OptionalHeader.SizeOfImage;
    for(int i=0;i<9;++i){
        auto* stub=reinterpret_cast<BYTE*>(GetProcAddress(client,names[i]));
        Check(stub!=reinterpret_cast<BYTE*>(before[i]),"export redirected");
        Check(stub>=base && stub+8<=base+imageSize,"stub lies inside the client image");
        Check(stub[0]==0xff && stub[1]==0x25,"stub is jmp [target]");
        void** target=nullptr;std::memcpy(&target,stub+2,4);
        Check(*target==reinterpret_cast<void*>(GetProcAddress(uhl,adapters[i])),"stub reaches the matching UHL_ function");
    }
    Check(GetProcAddress(client,"HUD_Redraw")==redrawBefore,"other exports untouched");
    std::cout<<"Runtime attach: stock client.dll adapted in memory only; nine exports reach UHL_* through in-image stubs.\n";
}
