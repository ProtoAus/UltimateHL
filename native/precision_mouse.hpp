#pragma once

// Runtime-only correction for the fingerprinted 3248/WON client's integer
// mouse scaling. Does not replace its look/strafe/pitch-limit logic or raw input.
namespace PrecisionMouse {
bool Install(void* client);
bool Installed();
void Shutdown();
}
