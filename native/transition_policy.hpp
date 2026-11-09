#pragma once
// Neither an active command from the old level nor return from a nested load
// completes a transition while the shared GameUI loading lifecycle is open.
inline bool TransitionMayResume(bool active,unsigned commandDepth,bool guiLoading) {
    return active && commandDepth==0 && !guiLoading;
}
