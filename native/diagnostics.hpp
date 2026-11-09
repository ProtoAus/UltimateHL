#pragma once
#include <cstdint>

struct UhlCvar {
    char* name;
    char* string;
    int flags;
    float value;
    UhlCvar* next;
};

// Diagnostics never replay inputs or write engine state. Trace files are local,
// opt-in and include key codes (not entered text); disable them for real runs.
namespace Diagnostics {
void Initialize(void* engineFunctions);
void EnsureCommands();
void Report();
void StartTrace();
void StopTrace();
void Event(const char* kind, std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0);
void UserCommand(const void* command, bool active);
void Shutdown();
bool Readable(const void* address, unsigned bytes);
}
