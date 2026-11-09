#pragma once

namespace MouseSettings {
// Install before calling the original client's Initialize. Deliberately remains
// installed through HUD_Shutdown: the stock cached-value restore is also a write.
bool Install(void* clientModule);
bool Protected();
unsigned BlockedWrites();
void Status(void (*print)(const char*));
}
