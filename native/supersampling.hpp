#pragma once
namespace Supersampling {
void Initialize(void* functions);
void EnsureControls();
void Frame();
bool Ready();
void SetScale(int scale);
void ApplyLaunchScale();
void Status();
void Shutdown();
}
