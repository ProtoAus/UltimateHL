#include "../native/cheat_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
static void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    Check(MaySyncCheats(true,1,1,0,0,1),"local SP can honor intentional enable");
    Check(MaySyncCheats(true,1,1,0,0,0),"local SP can honor intentional disable");
    Check(!MaySyncCheats(false,1,1,0,0,1),"opt-out preserves stock latch");
    Check(!MaySyncCheats(true,0,1,0,0,1),"no local server / remote connection untouched");
    Check(!MaySyncCheats(true,1,2,0,0,1),"multiplayer untouched");
    Check(!MaySyncCheats(true,1,1,1,0,1) && !MaySyncCheats(true,1,1,0,1,1),"deathmatch/coop untouched");
    Check(!MaySyncCheats(true,2,1,0,0,1),"unknown server state untouched");
    Check(!MaySyncCheats(true,1,1,0,0,std::numeric_limits<float>::quiet_NaN()),"nonfinite request untouched");
    std::cout<<"Local practice cheat latch permission policy passed.\n";
}
