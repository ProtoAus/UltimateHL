#pragma once
struct CursorPolicy {
    bool foreground=false,console=false,gameplayMouse=false,loading=false,allowLoadLock=false;
    bool confineEnabled=true,freezeEnabled=true;
    bool Freeze()const { return foreground && !console && loading && allowLoadLock && freezeEnabled; }
    bool Confine()const { return foreground && !console && confineEnabled && (gameplayMouse || (loading && allowLoadLock)); }
    // The client's gameplay mouse pass sees no motion and never warps the pointer when the
    // view must not turn. Re-focusing with the console open re-activates the stock mouse.
    bool NeutralLook()const { return !foreground || console || Freeze(); }
};
