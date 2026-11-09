#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" {
__declspec(dllexport) int mockMouseActive = 0;
__declspec(dllexport) int mockCalls = 0;
__declspec(dllexport) POINT mockCursor{};
// Enabled only after mock_host replaces this client's SPI import with a fake.
// An unpatched mock must never write real Windows preferences.
__declspec(dllexport) int mockMousePolicyProbeEnabled = 0;
__declspec(dllexport) BOOL __cdecl MockTryMouseWrites() {
    if (!mockMousePolicyProbeEnabled) return FALSE;
    int accelerationOn[3]{6, 10, 1}, accelerationOff[3]{0, 0, 0};
    return SystemParametersInfoA(SPI_SETMOUSE, 0, accelerationOn, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) &&
           SystemParametersInfoA(SPI_SETMOUSE, 0, accelerationOff, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) &&
           SystemParametersInfoA(SPI_SETMOUSESPEED, 0, reinterpret_cast<PVOID>(10), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
__declspec(dllexport) BOOL __cdecl MockReadMouse(int* values) { return SystemParametersInfoA(SPI_GETMOUSE, 0, values, 0); }
__declspec(dllexport) BOOL __cdecl MockOtherSetting() {
    if (!mockMousePolicyProbeEnabled) return FALSE;
    return SystemParametersInfoA(SPI_SETBEEP, TRUE, nullptr, 0); // Fake import only.
}

__declspec(dllexport) int __cdecl Initialize(void*, int version) {
    ++mockCalls;
    if (mockMousePolicyProbeEnabled && !MockTryMouseWrites()) return 0;
    return version == 7;
}
__declspec(dllexport) void __cdecl IN_Accumulate() {
    ++mockCalls;
    GetCursorPos(&mockCursor);
    // Import both APIs without moving the desktop cursor in the test.
    if (mockMouseActive == -1) SetCursorPos(mockCursor.x, mockCursor.y);
}
__declspec(dllexport) void __cdecl CL_CreateMove(float, void*, int) { ++mockCalls; }
__declspec(dllexport) void __cdecl HUD_Frame(double) { ++mockCalls; }
__declspec(dllexport) void __cdecl HUD_Shutdown() { ++mockCalls; }
__declspec(dllexport) void __cdecl HUD_Init() { ++mockCalls; }
__declspec(dllexport) void __cdecl IN_ClearStates() { ++mockCalls; }
__declspec(dllexport) int __cdecl HUD_Key_Event(int, int, const char*) { ++mockCalls; return 42; }
__declspec(dllexport) void __cdecl IN_MouseEvent(int) { ++mockCalls; }
__declspec(dllexport) BOOL __cdecl MockReadCursor(POINT* point) { return GetCursorPos(point); }
}
