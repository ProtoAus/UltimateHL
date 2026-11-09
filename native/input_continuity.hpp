#pragma once
#include <cstdint>

namespace InputContinuity {
void Initialize(void* engineFunctions);
void EnsureControls();
void BeginLoad();
void EndLoad();
void BeforeCreateMove(bool active);
void AfterCreateMove(bool active);
int ClientKey(int down, int key, const char* binding, int originalResult);
void Cancel(const char* reason);
bool DirectFpsMessage();
void Status();
void Shutdown();

struct MessageContext { bool valid = false; bool down = false; unsigned vk = 0; };
class MessageScope {
public:
    MessageScope(unsigned message, std::uintptr_t wParam, std::uintptr_t lParam);
    ~MessageScope();
private:
    MessageContext previous_;
};
}
