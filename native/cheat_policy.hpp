#pragma once
#include <cmath>
// This is a local practice convenience, not permission to alter remote or
// multiplayer servers. Unknown/transitional/non-finite states fail closed.
inline bool MaySyncCheats(bool enabled,int active,int maxClients,float deathmatch,float coop,float requested) {
    return enabled && active==1 && maxClients==1 && deathmatch==0 && coop==0 && std::isfinite(requested);
}
