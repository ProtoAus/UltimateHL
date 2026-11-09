#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include "settings.hpp"
#include "engine_bridge.hpp"
#include "diagnostics.hpp"
#include "presentation.hpp"
#include "cursor_guard.hpp"
#include "supersampling.hpp"
#include "scene_target.hpp"
namespace {
void* api[87]{};
HWND panel=nullptr;HFONT font=nullptr;int dpi=96;
constexpr wchar_t className[]=L"UltimateHLSettings37";
enum { Scale=101,Clip,Freeze,FastLoads,LoadMenu,Raw,HudPixels,Msaa,HudArt,Apply,Done,ObsHelp,HudScale=115,Crosshair,EmptyCrosshair,Bright,GammaLock,Precise,SmoothText };
constexpr int BrightnessValue=489;
const wchar_t* const styles[]={L"original",L"ultimatehl"}; // crosshair styles; same order as tools/crosshair_pack.py
const char* const names[]={"uhl_render_scale","uhl_cursor_lock","uhl_load_mouse_freeze","uhl_load_fps_min",
    "uhl_load_menu","uhl_rawinput","uhl_hud_texels","uhl_msaa","gl_spriteblend","uhl_cheats_live","uhl_hud_scale",
    "uhl_brightness","uhl_gamma_lock","bxt_precise_fps_limit"};
template<class T>T Fn(unsigned i){return reinterpret_cast<T>(api[i]);}
std::filesystem::path GamePath() {
    wchar_t file[MAX_PATH]{};GetModuleFileNameW(nullptr,file,MAX_PATH);return std::filesystem::path(file).parent_path();
}
UhlCvar* Get(const char* name) { return api[72] ? Fn<UhlCvar* (__cdecl*)(const char*)>(72)(name) : nullptr; }
float Value(const char* name,float fallback=0) { auto* var=Get(name);return var ? var->value : fallback; }
void Set(const char* name,float value) {
    if (api[37]) Fn<void (__cdecl*)(char*,float)>(37)(const_cast<char*>(name),value);
}
int Px(int value){return MulDiv(value,dpi,96);}
HWND Control(const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height) {
    HWND child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,Px(x),Px(y),Px(width),Px(height),panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(L"UHLCompat.dll"),nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
}
void Check(int id,bool checked){SendDlgItemMessageW(panel,id,BM_SETCHECK,checked ? BST_CHECKED : BST_UNCHECKED,0);}
bool Checked(int id){return SendDlgItemMessageW(panel,id,BM_GETCHECK,0,0)==BST_CHECKED;}
bool SameFile(const std::filesystem::path& left,const std::filesystem::path& right) {
    FILE *a=nullptr,*b=nullptr;
    if (_wfopen_s(&a,left.c_str(),L"rb") || !a)return false;
    if (_wfopen_s(&b,right.c_str(),L"rb") || !b){fclose(a);return false;}
    bool same=true;char one[4096]{},two[4096]{};
    while (same) {
        const auto n=fread(one,1,sizeof(one),a),m=fread(two,1,sizeof(two),b);
        same=n==m && !std::memcmp(one,two,n);if (!n || !m)break;
    }
    same=same && !ferror(a) && !ferror(b);fclose(a);fclose(b);return same;
}
std::filesystem::path CrosshairPreset(int index){return GamePath().parent_path()/L"HUD-presets/crosshairs"/styles[index]/L"crosshairs.spr";}
std::filesystem::path LiveCrosshairs(){return GamePath()/L"valve_WON/sprites/crosshairs.spr";}
bool ReadAll(const std::filesystem::path& path,std::vector<unsigned char>& data) {
    FILE* file=nullptr;if (_wfopen_s(&file,path.c_str(),L"rb") || !file)return false;
    data.clear();unsigned char block[4096];size_t n=0;
    while ((n=fread(block,1,sizeof(block),file))>0 && data.size()<(16u<<20))data.insert(data.end(),block,block+n);
    const bool ok=!ferror(file);fclose(file);return ok;
}
// The live crosshairs.spr is a style preset whose slot 1 (top-left 24x24 cell) holds the
// crosshair for weapons without their own; BXT's start-of-run crosshair also shows slot 1.
// Choice 0 keeps the style's slot 1, 1 copies the glock's crosshair there, 2 empties it.
constexpr size_t SprFrame=810,SprPixels=830; // IDSP v2: header 40, palette size + 768, frame header 20
int ReadInt(const std::vector<unsigned char>& data,size_t at){int value=0;std::memcpy(&value,data.data()+at,4);return value;}
POINT GlockCell() {
    POINT cell{24,0}; // Half-Life's; an edited weapon list wins
    FILE* file=nullptr;if (_wfopen_s(&file,(GamePath()/L"valve_WON/sprites/weapon_9mmhandgun.txt").c_str(),L"rb") || !file)return cell;
    char line[256]{};
    while (fgets(line,sizeof(line),file)) {
        char name[64]{},sprite[64]{};int res=0,x=0,y=0,w=0,h=0;
        if (sscanf_s(line,"%63s %d %63s %d %d %d %d",name,static_cast<unsigned>(sizeof(name)),&res,sprite,static_cast<unsigned>(sizeof(sprite)),&x,&y,&w,&h)==7 &&
            !std::strcmp(name,"crosshair") && res==640 && !std::strcmp(sprite,"crosshairs") && w==24 && h==24) { cell={x,y};break; }
    }
    fclose(file);return cell;
}
bool Compose(std::vector<unsigned char>& sheet,int choice) {
    if (sheet.size()<SprPixels || std::memcmp(sheet.data(),"IDSP",4) || ReadInt(sheet,4)!=2 || ReadInt(sheet,12)!=3 ||
        ReadInt(sheet,28)!=1 || sheet[40]!=0 || sheet[41]!=1 || ReadInt(sheet,SprFrame)!=0)return false; // single-frame alpha-test sheet
    const int width=ReadInt(sheet,SprFrame+12),height=ReadInt(sheet,SprFrame+16);
    if (width<24 || height<24 || sheet.size()!=SprPixels+static_cast<size_t>(width)*height)return false;
    const POINT glock=GlockCell();
    if (choice==1 && (glock.x<0 || glock.y<0 || glock.x+24>width || glock.y+24>height))return false;
    unsigned char* pixels=sheet.data()+SprPixels;
    for (int y=0;y<24;++y)for (int x=0;x<24;++x) {
        if (choice==1)pixels[y*width+x]=pixels[(glock.y+y)*width+glock.x+x];
        else if (choice==2)pixels[y*width+x]=255; // alpha-test transparent index
    }
    return true;
}
struct CrosshairState{int style,choice;}; // style 2: a hand-installed sheet, left alone
CrosshairState CurrentCrosshairs() {
    std::vector<unsigned char> current;
    if (!ReadAll(LiveCrosshairs(),current))return {0,0}; // no override: the stock valve sheet is used
    for (int style=0;style<2;++style) {
        std::vector<unsigned char> base;if (!ReadAll(CrosshairPreset(style),base))continue;
        for (int choice=0;choice<3;++choice) {
            auto sheet=base;
            if ((choice==0 || Compose(sheet,choice)) && sheet==current)return {style,choice};
        }
    }
    return {2,0};
}
bool SelectCrosshairs(int style,int choice) {
    if (style<0 || style>1)return true;
    std::vector<unsigned char> sheet,current;
    if (!ReadAll(CrosshairPreset(style),sheet) || (choice!=0 && !Compose(sheet,choice)))return false;
    const auto live=LiveCrosshairs();
    if (ReadAll(live,current) && current==sheet)return true;
    const auto temporary=live.wstring()+L".tmp";FILE* file=nullptr;
    if (_wfopen_s(&file,temporary.c_str(),L"wb") || !file)return false;
    const bool written=fwrite(sheet.data(),1,sheet.size(),file)==sheet.size();
    if (fclose(file) || !written || !MoveFileExW(temporary.c_str(),live.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());return false;
    }
    return true;
}
// Engine text font: platform/resource/TrackerScheme.res, original or with an antialiased
// EngineFont (tools/text_quality.py writes both presets).
std::filesystem::path TextPreset(bool smooth){return GamePath().parent_path()/L"HUD-presets/fonts"/(smooth ? L"smooth" : L"original")/L"TrackerScheme.res";}
std::filesystem::path LiveScheme(){return GamePath()/L"platform/resource/TrackerScheme.res";}
void ShowBrightness(int position) {
    wchar_t text[32]{};
    if (position==0)wcscpy_s(text,L"0 (off)");else swprintf_s(text,L"%+.2f",position/100.0);
    SetDlgItemTextW(panel,BrightnessValue,text);
}
void Refresh() {
    SendDlgItemMessageW(panel,Scale,CB_SETCURSEL,Value("uhl_render_scale")==2 ? 1 : 0,0);
    Check(Clip,Value(names[1])!=0);Check(Freeze,Value(names[2])!=0);Check(FastLoads,Value(names[3])>=20);
    Check(LoadMenu,Value(names[4])!=0);Check(Raw,Value(names[5])!=0);Check(HudPixels,Value(names[6])!=0);Check(Msaa,Value(names[7])!=0);
    const auto game=GamePath();
    const auto current=game/L"valve_WON/sprites/hud.txt";
    const bool compact=SameFile(current,game.parent_path()/L"HUD-presets/compact/hud.txt");
    const bool ag=SameFile(current,game.parent_path()/L"HUD-presets/ag/hud.txt");
    SendDlgItemMessageW(panel,HudArt,CB_SETCURSEL,ag ? 2 : (compact ? 1 : 0),0);
    const float hudScale=Value("uhl_hud_scale",1);
    WPARAM scaleIndex=0;for (WPARAM i=0;i<std::size(HudScales);++i)if (hudScale==HudScales[i])scaleIndex=i+1;
    SendDlgItemMessageW(panel,HudScale,CB_SETCURSEL,scaleIndex,0);
    HWND cross=GetDlgItem(panel,Crosshair),empty=GetDlgItem(panel,EmptyCrosshair);std::error_code error;
    const bool available=std::filesystem::exists(CrosshairPreset(0),error) && std::filesystem::exists(CrosshairPreset(1),error);
    const auto state=CurrentCrosshairs();
    if (state.style==2 && SendMessageW(cross,CB_GETCOUNT,0,0)<3)
        SendMessageW(cross,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Custom (keep the installed file)"));
    SendMessageW(cross,CB_SETCURSEL,static_cast<WPARAM>(state.style),0);EnableWindow(cross,available);
    SendMessageW(empty,CB_SETCURSEL,static_cast<WPARAM>(state.choice),0);EnableWindow(empty,available && state.style!=2);
    const int position=static_cast<int>(std::lround(std::clamp(Value("uhl_brightness"),-0.5f,1.5f)*100));
    SendDlgItemMessageW(panel,Bright,TBM_SETPOS,TRUE,position);ShowBrightness(position);
    Check(GammaLock,Value("uhl_gamma_lock",1)!=0);
    const bool precise=Get("bxt_precise_fps_limit")!=nullptr; // only the rebuilt BXT has it
    Check(Precise,precise && Value("bxt_precise_fps_limit")!=0);EnableWindow(GetDlgItem(panel,Precise),precise);
    const bool fonts=std::filesystem::exists(TextPreset(false),error) && std::filesystem::exists(TextPreset(true),error);
    Check(SmoothText,fonts && SameFile(LiveScheme(),TextPreset(true)));EnableWindow(GetDlgItem(panel,SmoothText),fonts);
}
bool SelectArt(int index) {
    const wchar_t* variants[]={L"stock",L"compact",L"ag"};if(index<0 || index>2)return false;
    const auto game=GamePath(),source=game.parent_path()/L"HUD-presets"/variants[index];
    WIN32_FIND_DATAW entry{};HANDLE find=FindFirstFileW((source/L"*.txt").c_str(),&entry);
    if (find==INVALID_HANDLE_VALUE)return false;
    bool ok=true;
    do {
        if (!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            const auto from=source/entry.cFileName,to=game/L"valve_WON/sprites"/entry.cFileName;
            if (!SameFile(from,to) && !CopyFileW(from.c_str(),to.c_str(),FALSE))ok=false;
        }
    }while(FindNextFileW(find,&entry));FindClose(find);return ok;
}
void ApplyChanges() {
    Supersampling::SetScale(SendDlgItemMessageW(panel,Scale,CB_GETCURSEL,0,0)==1 ? 2 : 1);
    Set(names[1],Checked(Clip) ? 1.0f : 0.0f);Set(names[2],Checked(Freeze) ? 1.0f : 0.0f);
    Set(names[3],Checked(FastLoads) ? 100.0f : 0.0f);Set(names[4],Checked(LoadMenu) ? 1.0f : 0.0f);
    Set(names[5],Checked(Raw) ? 1.0f : 0.0f);Set(names[6],Checked(HudPixels) ? 1.0f : 0.0f);Set(names[7],Checked(Msaa) ? 1.0f : 0.0f);
    Set("gl_spriteblend",Checked(HudPixels) ? 0.0f : 1.0f);
    const auto hudIndex=SendDlgItemMessageW(panel,HudScale,CB_GETCURSEL,0,0);
    Set("uhl_hud_scale",hudIndex>=1 && hudIndex<=static_cast<LRESULT>(std::size(HudScales)) ? HudScales[hudIndex-1] : 1.0f);
    if (!SelectArt(static_cast<int>(SendDlgItemMessageW(panel,HudArt,CB_GETCURSEL,0,0))))
        MessageBoxW(panel,L"HUD preset files could not be selected. Other settings still apply.",L"UltimateHL",MB_OK|MB_ICONWARNING);
    HWND cross=GetDlgItem(panel,Crosshair),empty=GetDlgItem(panel,EmptyCrosshair);
    const int choice=IsWindowEnabled(empty) ? static_cast<int>(SendMessageW(empty,CB_GETCURSEL,0,0)) : 0;
    if (IsWindowEnabled(cross) && !SelectCrosshairs(static_cast<int>(SendMessageW(cross,CB_GETCURSEL,0,0)),choice))
        MessageBoxW(panel,L"Crosshair files could not be selected. Other settings still apply.",L"UltimateHL",MB_OK|MB_ICONWARNING);
    Set("uhl_brightness",static_cast<float>(SendDlgItemMessageW(panel,Bright,TBM_GETPOS,0,0))/100.0f);
    Set("uhl_gamma_lock",Checked(GammaLock) ? 1.0f : 0.0f);
    if (IsWindowEnabled(GetDlgItem(panel,Precise)))Set("bxt_precise_fps_limit",Checked(Precise) ? 1.0f : 0.0f);
    if (IsWindowEnabled(GetDlgItem(panel,SmoothText))) {
        const auto from=TextPreset(Checked(SmoothText)),to=LiveScheme();
        if (!SameFile(from,to) && !CopyFileW(from.c_str(),to.c_str(),FALSE))
            MessageBoxW(panel,L"The engine font could not be changed. Other settings still apply.",L"UltimateHL",MB_OK|MB_ICONWARNING);
    }
    Settings::Save();CursorGuard::Update();
    SetDlgItemTextW(panel,490,L"Applied. HUD art, crosshair and font changes require a game restart. Other switches apply live.");
}
LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if (message==WM_COMMAND) {
        switch(LOWORD(wp)) {
            case Apply:ApplyChanges();return 0;
            case Done:ApplyChanges();DestroyWindow(hwnd);return 0;
            case Crosshair:
                if (HIWORD(wp)==CBN_SELCHANGE)
                    EnableWindow(GetDlgItem(hwnd,EmptyCrosshair),SendDlgItemMessageW(hwnd,Crosshair,CB_GETCURSEL,0,0)!=2);
                return 0;
            case ObsHelp:MessageBoxW(hwnd,
                L"Default: Big AG HUD art, 2x scene, Tankfird BXT and full-resolution DXGI output for OBS Display Capture. Only START HERE and windowed launchers are needed.\n\n"
                L"HUD 1.25x-2x enlarges health, armor, ammo and BXT's timer and speedometer on a separate canvas with premultiplied bilinear filtering; text, the crosshair and everything else stay at 1x (with the official BXT the whole HUD is enlarged). Menus/mouse coordinates and the 3D view are not scaled by this control.\n\n"
                L"Brightness brightens the finished 3D view with a curve that keeps black and white fixed; it does not touch lightmaps. The gamma lock holds gamma, lightgamma, texgamma and the legacy brightness at the engine defaults; while locked, the brightness command sets this control.\n\n"
                L"The precision frame timer (rebuilt BXT) starts each frame exactly on its fps_max deadline instead of up to a few milliseconds late. It never runs the game faster than fps_max.\n\n"
                L"Artwork and crosshair changes need a restart. HUD scale and brightness apply live. Unsupported GPU/layout checks keep the native HUD. uhl_status reports both scene and HUD paths.",
                L"Rendering, HUD and timing help",MB_OK|MB_ICONINFORMATION);return 0;
        }
    }
    if (message==WM_HSCROLL && reinterpret_cast<HWND>(lp)==GetDlgItem(hwnd,Bright)) { // live preview while dragging
        const int position=static_cast<int>(SendDlgItemMessageW(hwnd,Bright,TBM_GETPOS,0,0));
        Set("uhl_brightness",position/100.0f);ShowBrightness(position);return 0;
    }
    if (message==WM_CLOSE){DestroyWindow(hwnd);return 0;}
    if (message==WM_DESTROY){panel=nullptr;if(font){DeleteObject(font);font=nullptr;}return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
namespace Settings {
void Initialize(void* functions){std::memcpy(api,functions,sizeof(api));}
void EnsureCommands(){EngineBridge::EnsureCommand("uhl_settings",Open);EngineBridge::EnsureCommand("uhl_settings_save",Save);}
void Save() {
    if (!EngineBridge::Supported())return;
    const auto path=GamePath()/L"valve_WON/uhl_preferences.cfg";
    const auto temporary=path.wstring()+L".tmp";
    FILE* file=nullptr;if (_wfopen_s(&file,temporary.c_str(),L"wb") || !file)return;
    fputs("// UltimateHL saved preferences. Numeric whitelist only.\r\n",file);
    for (const auto* name:names) {
        const auto* var=Get(name);if (var && std::isfinite(var->value))fprintf(file,"%s %.9g\r\n",name,static_cast<double>(var->value));
    }
    const bool ok=!ferror(file);const int closed=fclose(file);
    if (!ok || closed || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temporary.c_str());
}
void Open() {
    if (!EngineBridge::Supported())return;
    if (panel){CursorGuard::Release();ShowWindow(panel,SW_RESTORE);return;}
    CursorGuard::Release();
    HWND owner=static_cast<HWND>(Presentation::GameWindow());if (!owner)return;
    using DpiFn=UINT(WINAPI*)(HWND);
    const auto getDpi=reinterpret_cast<DpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    dpi=getDpi ? static_cast<int>(getDpi(owner)) : 96;if(dpi<=0)dpi=96;
    WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(L"UHLCompat.dll");cls.lpszClassName=className;
    cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls); // brightness slider
    RECT rect{0,0,Px(620),Px(756)};AdjustWindowRect(&rect,WS_CAPTION|WS_SYSMENU,FALSE);
    panel=CreateWindowExW(WS_EX_CONTROLPARENT,className,L"UltimateHL settings",WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,owner,nullptr,cls.hInstance,nullptr);
    if(!panel)return;
    font=CreateFontW(-Px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    Control(L"STATIC",L"UltimateHL 3.7 - changes apply to this experimental copy",0,480,18,14,590,24);
    Control(L"STATIC",L"3D scene rendering",0,481,18,50,190,23);
    HWND scaleBox=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,Scale,215,46,380,100);
    SendMessageW(scaleBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Native 1x (no UHL supersampling)"));
    SendMessageW(scaleBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"2x each axis (default; HUD scale is independent)"));
    const wchar_t* labels[]={L"Confine cursor to focused gameplay (safe at 4 FPS)",L"Discard load-time mouse X/Y (pin with cursor lock on)",
        L"Optional 100 FPS load floor (default OFF for glitches)",L"Allow stock automatic main menu during loads",
        L"Native raw mouse input",L"Atlas-safe native sprite sampling (scaled HUD is filtered)",L"Legacy MSAA rasterization (DXGI: use 2x scene instead)"};
    for(int i=0;i<7;++i)Control(L"BUTTON",labels[i],BS_AUTOCHECKBOX|WS_TABSTOP,Clip+i,18,86+i*30,580,27);
    Control(L"STATIC",L"HUD artwork (restart required)",0,482,18,310,235,23);
    HWND art=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,HudArt,260,306,335,100);
    SendMessageW(art,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Original 640 artwork"));
    SendMessageW(art,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Compact native 320 artwork"));
    SendMessageW(art,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Big AG HUD (default; modern large artwork)"));
    Control(L"STATIC",L"HUD scale / filtering",0,485,18,346,235,23);
    HWND hudScale=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,HudScale,260,342,335,140);
    SendMessageW(hudScale,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"1x - native size (default)"));
    for(const auto* text:{L"1.25x - smooth, atlas-safe filtering",L"1.5x - smooth, atlas-safe filtering",
                          L"1.75x - smooth, atlas-safe filtering",L"2x - smooth, atlas-safe filtering"})
        SendMessageW(hudScale,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    Control(L"STATIC",L"Crosshairs (restart required)",0,486,18,382,235,23);
    HWND cross=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,Crosshair,260,378,335,100);
    SendMessageW(cross,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Original Half-Life crosshairs"));
    SendMessageW(cross,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"UltimateHL crosshairs (default)"));
    Control(L"STATIC",L"No-crosshair weapons + run start",0,487,18,418,240,23);
    HWND empty=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,EmptyCrosshair,260,414,335,100);
    SendMessageW(empty,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Centre mark - slot 1 of the sheet (default)"));
    SendMessageW(empty,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Glock crosshair"));
    SendMessageW(empty,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"None"));
    Control(L"STATIC",L"Brightness (3D view, live)",0,488,18,454,235,23);
    HWND bright=Control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_AUTOTICKS|WS_TABSTOP,Bright,255,450,270,30);
    SendMessageW(bright,TBM_SETRANGE,TRUE,MAKELPARAM(-50,150));SendMessageW(bright,TBM_SETTICFREQ,25,0);
    SendMessageW(bright,TBM_SETLINESIZE,0,5);SendMessageW(bright,TBM_SETPAGESIZE,0,25);
    Control(L"STATIC",L"",0,BrightnessValue,530,454,70,23);
    Control(L"BUTTON",L"Lock gamma, lightgamma, texgamma and legacy brightness at defaults",BS_AUTOCHECKBOX|WS_TABSTOP,GammaLock,18,488,580,27);
    Control(L"BUTTON",L"Precision frame timer: frames start exactly on time (rebuilt BXT)",BS_AUTOCHECKBOX|WS_TABSTOP,Precise,18,518,580,27);
    Control(L"BUTTON",L"Smooth engine text: antialiased font (restart required)",BS_AUTOCHECKBOX|WS_TABSTOP,SmoothText,18,548,580,27);
    Control(L"STATIC",L"Mouse lock releases for Alt-Tab, console and this settings window.\nLoad freeze discards mouse motion, not the saved camera orientation.",0,483,18,582,580,40);
    Control(L"BUTTON",L"Rendering, HUD and timing help",BS_PUSHBUTTON|WS_TABSTOP,ObsHelp,18,626,300,30);
    Control(L"STATIC",L"Display presentation is chosen by launcher and requires a restart.",0,484,18,664,590,22);
    Control(L"STATIC",L"Ready. Preferences are saved locally and restored on next launch.",0,490,18,688,590,25);
    Control(L"BUTTON",L"Apply",BS_PUSHBUTTON|WS_TABSTOP,Apply,380,718,100,28);
    Control(L"BUTTON",L"OK",BS_DEFPUSHBUTTON|WS_TABSTOP,Done,495,718,100,28);
    Refresh();ShowWindow(panel,SW_SHOWNORMAL);
}
void Shutdown(){Save();if(panel)DestroyWindow(panel);UnregisterClassW(className,GetModuleHandleW(L"UHLCompat.dll"));}
}
