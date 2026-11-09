#pragma once
namespace CursorGuard {
void Initialize(void* functions,const int* mouseActive);
void EnsureControls();
void Update();
bool FreezeMouse();
bool NeutralLook();
void Release();
void Status();
void Shutdown();
}
