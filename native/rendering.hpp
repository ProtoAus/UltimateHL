#pragma once
namespace Rendering {
void Initialize(void* engineFunctions);
void EnsureControls();
void Frame();
void Status();
void SelfTest();
void Shutdown();
bool Ready();
unsigned NativeUploads();
unsigned SafeMips();
}
