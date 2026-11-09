#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// Parse ONLY an explicit single-command binding. Never execute aliases,
// separators, arbitrary console text or a recursive command buffer from a UI message.
inline bool ParseFpsBinding(const char* text,float& value) {
    if (!text) return false;
    while (*text==' ' || *text=='\t') ++text;
    if (std::strncmp(text,"uhl_fps",7) || (text[7]!=' ' && text[7]!='\t')) return false;
    text+=7;
    char* end=nullptr;value=std::strtof(text,&end);
    if (end==text || !std::isfinite(value) || value<1 || value>100) return false;
    while (*end==' ' || *end=='\t') ++end;
    return !*end;
}

// Spans the synchronous handler AND its asynchronous UI/reconnect tail. Choices
// during a floored load select post-load FPS without slowing the loading frames.
class LoadFpsSession {
public:
    bool active=false;
    float desired=0,lastApplied=0;
    void Begin(float current) {
        if (!active) { desired=current;lastApplied=current;active=true; }
        else Observe(current);
    }
    void Observe(float current) {
        if (active && std::isfinite(current) && current>0 && current!=lastApplied) desired=current;
    }
    void Select(float value) { desired=value; }
    float Effective(float minimum) const {
        return active && std::isfinite(minimum) && minimum>=20 && minimum<=100 && desired<minimum ? minimum : desired;
    }
    void Applied(float value) { lastApplied=value; }
    float Finish() { active=false;return desired; }
};

// Historical v3 decision model: minimum is opt-in, loading scope only. Explicit
// uhl_fps commands win, including when the requested value equals the floor.
struct LoadFpsGuard {
    float before = 0;
    float applied = 0;
    std::uint64_t revision = 0;
    bool changed = false;

    void Begin(float current, float minimum, std::uint64_t serial) {
        before = current;
        applied = current;
        revision = serial;
        changed = std::isfinite(current) && std::isfinite(minimum) &&
                  current > 0 && minimum >= 20 && minimum <= 100 && current < minimum;
        if (changed) applied = minimum;
    }
    bool Restore(float current, std::uint64_t serial) const {
        return changed && serial == revision && current == applied;
    }
};
