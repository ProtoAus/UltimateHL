#include "../native/cursor_policy.hpp"
#include <cstdlib>
#include <iostream>
static void Check(bool value,const char* label){if(!value){std::cerr<<label<<'\n';std::exit(1);}}
int main(){
    CursorPolicy p;p.foreground=true;p.gameplayMouse=true;
    Check(p.Confine() && !p.Freeze(),"gameplay confines without freezing");
    p.gameplayMouse=false;p.loading=true;p.allowLoadLock=true;
    Check(p.Confine() && p.Freeze(),"load stays confined even when stock mouse is deactivated");
    p.foreground=false;Check(!p.Confine() && !p.Freeze(),"Alt-Tab/settings always release");
    p.foreground=true;p.console=true;Check(!p.Confine() && !p.Freeze(),"console always releases");
    p.console=false;p.allowLoadLock=false;Check(!p.Confine() && !p.Freeze(),"deliberate load-menu opening releases");
    p.allowLoadLock=true;p.confineEnabled=false;Check(!p.Confine() && p.Freeze(),"confinement off does not secretly pin cursor");
    p.freezeEnabled=false;Check(!p.Freeze(),"freeze opt-out");
    p.confineEnabled=true;Check(p.Confine() && !p.Freeze(),"confine without discarding input");
    CursorPolicy look;look.foreground=true;look.gameplayMouse=true;
    Check(!look.NeutralLook(),"focused gameplay turns the view");
    look.console=true;Check(look.NeutralLook(),"console open (also after re-focusing) never turns the view");
    look.console=false;look.foreground=false;Check(look.NeutralLook(),"unfocused never turns the view");
    look.foreground=true;look.loading=true;look.allowLoadLock=true;Check(look.NeutralLook(),"pinned load never turns the view");
    std::cout<<"Cursor focus/console/load/menu/opt-out policy passed. No cursor API invoked.\n";
}
