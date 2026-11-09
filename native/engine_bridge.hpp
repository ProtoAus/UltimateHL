#pragma once
namespace EngineBridge {
// Requires the exact 3248 hw.dll fingerprint and a validated command-node ABI.
// Command slots and the exact GameUI007 activation slot are restored on shutdown.
void Initialize(void* engineFunctions);
void EnsureControls();
void Frame();
void BeforeCreateMove(bool active);
void ClientClearStates();
void AllowMenu();
bool FpsBinding(const char* binding);
bool EnsureCommand(const char* name, void (__cdecl* handler)());
bool HasCommand(const char* name);
void* CommandHandler(const char* name);
bool ReplaceCommand(const char* name,void* expected,void* replacement);
bool Supported();
bool SupportedServer(void* module);
bool SupportedBxt(void* module);
bool FileMatches(const wchar_t* path,const unsigned char (&sha256)[32]);
bool Loading();
bool ConsoleOpen(); // the old console, GameUI's console window or a GameUI menu
bool CopyConsoleSelection(); // Ctrl+C in GameUI's console: what right-click Copy does
bool ProtectLoadMouse();
const char* KeyBinding(int key);
bool BindingMapReady();
void Status();
void Shutdown();
}
