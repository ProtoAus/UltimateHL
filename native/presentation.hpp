#pragma once
namespace Presentation {
void Initialize(void* engineFunctions);
void EnsureControls();
void Frame();
void* GameWindow();
void Status();
void FocusLost();
void Shutdown();
}
