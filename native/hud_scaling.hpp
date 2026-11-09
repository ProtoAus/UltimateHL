#pragma once
namespace HudScaling {
void Initialize(void* functions);
void EnsureControls();
void Frame();
bool Active();
int Pass(); // 0 = not split, 1 = enlarged elements on the canvas, 2 = everything else at 1x
int HeightDelta();
int Width();int Height();
void Status();
void Shutdown();
}
