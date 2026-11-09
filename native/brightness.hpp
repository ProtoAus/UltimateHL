#pragma once
namespace Brightness {
void Initialize(void* functions);
void EnsureControls();
void Frame();      // keeps the legacy lighting cvars at defaults; turns `brightness` into uhl_brightness
void AfterScene(); // brightens the finished 3D view (before the HUD, console and menus)
void Status();
void Shutdown();
}
