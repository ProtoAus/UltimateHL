#pragma once
namespace DisplayPresenter {
void Initialize(void* functions);
void Frame();
bool Capturing();
void Status();
void DumpFrame();
void Shutdown();
}
