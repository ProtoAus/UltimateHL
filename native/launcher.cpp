#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static std::wstring Quote(const std::wstring& value) {
    if (!value.empty() && value.find_first_of(L" \t\n\v\"") == std::wstring::npos) return value;
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') { result.append(slashes * 2 + 1, L'\\'); result += ch; }
        else { result.append(slashes, L'\\'); result += ch; }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
static int Fail(const std::wstring& text) {
    std::wcerr << text << L"\n";
    MessageBoxW(nullptr, text.c_str(), L"UltimateHL launcher", MB_OK | MB_ICONERROR);
    return 1;
}
static bool HalfLifeRunning() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snapshot==INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if (Process32FirstW(snapshot,&entry)) do {
        if (!_wcsicmp(entry.szExeFile,L"hl.exe")) { found=true;break; }
    } while (Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
static bool CaptureMode(int& width,int& height) {
    const DWORD desktopWidth=static_cast<DWORD>(width),desktopHeight=static_cast<DWORD>(height);
    unsigned long long best=0;
    // 3248 rejects arbitrary sizes (e.g. desktop minus two) and silently falls
    // back to 640x480. Pick a real mode without ever switching the display.
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    for (DWORD index=0;EnumDisplaySettingsW(nullptr,index,&mode);++index) {
        if (mode.dmBitsPerPel<24 || mode.dmPelsWidth<640 || mode.dmPelsHeight<480 ||
            mode.dmPelsWidth>desktopWidth || mode.dmPelsHeight>desktopHeight ||
            (mode.dmPelsWidth==desktopWidth && mode.dmPelsHeight==desktopHeight)) continue;
        const auto area=static_cast<unsigned long long>(mode.dmPelsWidth)*mode.dmPelsHeight;
        if (area>best) { best=area;width=static_cast<int>(mode.dmPelsWidth);height=static_cast<int>(mode.dmPelsHeight); }
    }
    return best!=0;
}
int wmain(int argc, wchar_t** argv) {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const fs::path root = fs::path(module).parent_path();
    const fs::path game = root / L"Half-Life";
    bool bxt = true, windowed = false, captureSafe = false;
    int presentOverride=-1;
    std::wstring renderScale;
    std::vector<std::wstring> extra;
    for (int i = 1; i < argc; ++i) {
        std::wstring argument = argv[i];
        if (argument == L"--bxt") bxt = true;
        else if (argument == L"--no-bxt") bxt = false;
        else if (argument == L"--windowed") windowed = true;
        else if (argument == L"--capture-safe") captureSafe = true;
        else if (argument == L"--legacy-present") presentOverride=0;
        else if (argument == L"--dxgi-present") presentOverride=1;
        else if (argument == L"--render-scale" && i+1<argc) {
            renderScale=argv[++i];
            if (renderScale!=L"1" && renderScale!=L"2") return Fail(L"--render-scale requires 1 or 2");
        }
        else if (argument == L"--") { while (++i < argc) extra.emplace_back(argv[i]); break; }
        else return Fail(L"Usage: UltimateHL.exe [--no-bxt] [--windowed | --capture-safe] [--render-scale 1|2] [--legacy-present | --dxgi-present] [-- additional engine arguments]");
    }
    for (const auto* required : {L"hl.exe", L"hl.exe.manifest", L"UHLCompat.dll", L"valve_WON\\cl_dlls\\client.dll", L"valve_WON\\uhl_defaults.cfg"})
        if (!fs::exists(game / required)) return Fail(L"Incomplete v3.7.1 installation; missing " + std::wstring(required));
    if (!fs::exists(root / L"stage-manifest.json")) return Fail(L"Missing stage manifest. Re-stage into a NEW folder.");
    // Query physical desktop dimensions. The separate game process establishes
    // its own system DPI awareness via its manifest before creating its window.
    if (bxt && HalfLifeRunning()) return Fail(L"Close the running Half-Life first. BXT targets hl.exe by name; nothing was launched or modified.");
    if (windowed && captureSafe) return Fail(L"Choose ordinary windowed OR capture-safe borderless, not both.");
    SetProcessDPIAware();
    int width = windowed ? 1280 : GetSystemMetrics(SM_CXSCREEN);
    int height = windowed ? 800 : GetSystemMetrics(SM_CYSCREEN);
    if (captureSafe && !CaptureMode(width,height)) return Fail(L"No smaller supported capture-safe mode found. Use normal borderless with OBS Windows Graphics Capture.");
    const bool dxgi=presentOverride<0 ? (!windowed && !captureSafe) : presentOverride!=0;
    SetEnvironmentVariableW(L"UHL_DXGI_PRESENT",dxgi ? L"1" : L"0");
    SetEnvironmentVariableW(L"UHL_START_RENDER_SCALE",renderScale.empty() ? nullptr : renderScale.c_str());
    SetEnvironmentVariableW(L"UHL_RENDER_WIDTH",std::to_wstring(width).c_str());
    SetEnvironmentVariableW(L"UHL_RENDER_HEIGHT",std::to_wstring(height).c_str());
    SetEnvironmentVariableW(L"UHL_BORDERLESS", windowed ? L"0" : L"1");
    SetEnvironmentVariableW(L"UHL_CAPTURE_SAFE", captureSafe ? L"1" : L"0");
    SetEnvironmentVariableW(L"BXT_DISABLE_VSYNC", L"1");
    SetEnvironmentVariableW(L"BXT_LOGFILE", (root / L"bxt-playtest.log").c_str());
    const std::wstring token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    SetEnvironmentVariableW(L"UHL_LAUNCH_TOKEN", token.c_str());
    fs::path program = bxt ? root / L"BXT\\Injector.exe" : game / L"hl.exe";
    if (!fs::exists(program)) return Fail(L"Missing launcher input: " + program.wstring());
    std::vector<std::wstring> args{program.wstring()};
    if (bxt) args.emplace_back(L"hl.exe");
    for (const auto* argument : {L"-steam", L"-insecure", L"-nomaster", L"-game", L"valve_WON", L"-gl", L"-32bpp", L"-noforcemparms", L"-noforcemaccel", L"-noforcemspd", L"-windowed", L"-w"})
        args.emplace_back(argument);
    args.push_back(std::to_wstring(width));
    args.emplace_back(L"-h"); args.push_back(std::to_wstring(height));
    args.emplace_back(L"-condebug");
    // Config/userconfig apply the defaults. Keep this before forwarded scripts
    // too, so a diagnostic +map can't upload textures with stale settings.
    args.emplace_back(L"+exec"); args.emplace_back(L"uhl_defaults.cfg");
    args.insert(args.end(), extra.begin(), extra.end());
    // Apply once after startup CFGs, so subsequent console/UI choices win.
    // The first scene also consumes this intent if a forwarded script waits.
    args.emplace_back(L"+uhl_apply_launch");
    std::wstring command;
    for (const auto& argument : args) { if (!command.empty()) command += L' '; command += Quote(argument); }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, game.c_str(), &startup, &process))
        return Fail(L"Could not start the game. Win32 error " + std::to_wstring(GetLastError()));
    CloseHandle(process.hThread);
    const fs::path receipt = game / (L"uhl-startup-" + token + L".txt");
    bool verified = false;
    for (unsigned i = 0; i < 300; ++i) {
        if (fs::exists(receipt)) {
            std::ifstream file(receipt);
            std::string text((std::istreambuf_iterator<char>(file)), {});
            if (text.find("hud_ready=1") != std::string::npos &&
                text.find("windows_mouse_settings=protected") != std::string::npos) { verified = true; break; }
        }
        if (!bxt && WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        Sleep(100);
    }
    CloseHandle(process.hProcess);
    if (!verified) return Fail(L"The game did not confirm v3 initialization and Windows mouse-settings protection.\n"
        L"Use this UltimateHL.exe launcher (not Half-Life\\hl.exe) and keep -game valve_WON.\n"
        L"No existing game process was terminated. Check Half-Life\\qconsole.log.\n"
        L"Custom commands belong to the native client, not the DPI manifest.");
    std::wcout << L"UltimateHL v3.7.1 client and Windows mouse-settings protection verified; console: uhl_help / uhl_status\n"
               << L"Startup report: " << receipt << L"\n";
    return 0;
}
