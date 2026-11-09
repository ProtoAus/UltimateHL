#pragma once
#include <array>
#include <cstring>

inline bool RecoverableActionName(const char* name) {
    return name && std::strcmp(name,"+klook") && std::strcmp(name,"+mlook");
}
inline bool MatchingVirtualKey(unsigned expected,unsigned actual) {
    return expected != 0 && expected == actual;
}

struct ClientButton { int down[2]; int state; };
static_assert(sizeof(ClientButton) == 12);

inline bool RestoreHeldButton(ClientButton& button, int key, bool continuouslyHeld) {
    if (key <= 0 || key >= 256) return false;
    if (button.down[0] != key && button.down[1] != key) {
        if (!button.down[0]) button.down[0] = key;
        else if (!button.down[1]) button.down[1] = key;
        else return false; // Never overwrite a third unrelated owner.
    }
    if (continuouslyHeld) button.state = (button.state & ~6) | 1;
    else if (!(button.state & 1)) button.state |= 3; // A genuine new press retains its impulse.
    return true;
}

inline bool ReleaseButtonOwner(ClientButton& button, int key) {
    bool found = false;
    for (int& owner : button.down) if (owner == key) { owner = 0; found = true; }
    if (found && !button.down[0] && !button.down[1]) button.state = (button.state & ~1) | 4;
    return found;
}

// A repair can outlive the short load guard. Keep its owner until the real
// release/focus cancellation, including when the engine swallows that callback.
class RepairedButtonOwner {
public:
    void Remember(ClientButton& button, int key) {
        if (button_ && button_ != &button) ReleaseButtonOwner(*button_,key);
        button_ = &button;
    }
    bool Release(int key) {
        const bool released = button_ && ReleaseButtonOwner(*button_,key);
        button_ = nullptr;
        return released;
    }
    bool Active() const { return button_ != nullptr; }
private:
    ClientButton* button_ = nullptr;
};

// Tracks accepted engine key edges, not bound commands. The only edges consumed
// are synthetic releases of an already-held, supported button during loading.
// A real key-up, a released physical key, UI consumption and focus loss win.
class LoadKeyState {
public:
    struct Key { bool down = false; bool eligible = false; bool retained = false; };
    std::array<Key, 256> keys{};

    void Begin() { if (depth_++ == 0) cancelled_ = false; resuming_ = 0; }
    void End() { if (depth_) --depth_; if (!depth_ && !cancelled_) resuming_ = 2; }
    void FirstCommandFinished() { if (!depth_ && resuming_) --resuming_; }
    bool Armed() const { return !cancelled_ && (depth_ || resuming_); }

    bool Accept(int key, bool down, bool eligibleBinding, bool originalAccepted,
                bool enabled, bool ownsInput, bool physicalDown, bool actualKeyUp) {
        if (key < 0 || key >= static_cast<int>(keys.size()) || !originalAccepted) return false;
        auto& state = keys[static_cast<unsigned>(key)];
        if (!down && enabled && ownsInput && Armed() && state.down && state.eligible &&
            eligibleBinding && physicalDown && !actualKeyUp) {
            state.retained = true;
            return true; // Return 0 from HUD_Key_Event: do not dispatch a false -binding.
        }
        state.down = down;
        state.eligible = down && eligibleBinding;
        state.retained = false;
        return false;
    }
    bool TakeRetained(int key) {
        if (!keys[static_cast<unsigned>(key)].retained) return false;
        keys[static_cast<unsigned>(key)].retained = false;
        return true;
    }
    void Cancel() {
        for (auto& state : keys) state = {};
        cancelled_ = true;
        resuming_ = 0;
    }
private:
    unsigned depth_ = 0;
    unsigned resuming_ = 0;
    bool cancelled_ = false;
};
