#include "../native/load_key_state.hpp"
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
static void Down(LoadKeyState& state, int key) {
    Check(!state.Accept(key, true, true, true, true, true, true, false), "normal down is never consumed");
}
int main() {
    Check(RecoverableActionName("+forward") && RecoverableActionName("+back"), "movement is recoverable");
    Check(!RecoverableActionName("+klook") && !RecoverableActionName("+mlook") && !RecoverableActionName(nullptr), "never synthesize look modes");
    Check(MatchingVirtualKey(45,45) && !MatchingVirtualKey(45,116) && !MatchingVirtualKey(0,116), "never learn Insert from a save-key or unrelated callback");
    LoadKeyState state;
    Down(state, 'w'); Down(state, 'a');
    Check(!state.Accept('w', false, true, true, true, true, true, false), "stock release outside transition");
    Down(state, 'w');
    state.Begin();
    Check(state.Accept('w', false, true, true, true, true, true, false), "retain synthetic W release during load");
    Check(state.Accept('a', false, true, true, true, true, true, false), "retain A independently; not only latest key");
    Check(state.keys['w'].down && state.keys['a'].down, "both logical held owners remain down");
    state.End();
    Check(state.TakeRetained('w') && state.TakeRetained('a'), "reconcile every consumed release once");
    Check(!state.TakeRetained('w'), "no duplicate re-press");
    Check(state.Accept('a', false, true, true, true, true, true, false), "protect late resume clear before first usercmd");
    state.FirstCommandFinished();
    Check(state.Armed(), "late reset protected through two resumed commands");
    state.FirstCommandFinished();
    Check(!state.Accept('w', false, true, true, true, true, true, false), "normal clearing after resume protection ends");

    state = {};
    Down(state, 'w'); Down(state, 'a'); state.Begin();
    Check(!state.Accept('w', false, true, true, true, true, false, false), "physical release during load is honored");
    Check(!state.keys['w'].down, "no stuck released key");
    Check(!state.Accept('a', false, true, true, true, true, true, true), "actual queued key-up beats newer physical down");
    Check(!state.keys['a'].down, "real event ordering wins");
    Down(state, 'a');
    Check(!state.Accept('a', false, true, true, false, true, true, false), "disabled option preserves stock behavior");
    Down(state, 'a');
    Check(!state.Accept('a', false, true, true, true, false, true, false), "no background/UI-owner preservation");
    Down(state, 'a');
    Check(!state.Accept('a', false, false, true, true, true, true, false), "arbitrary aliases/rebindings not replayed");
    Down(state, 'a');
    Check(!state.Accept('a', false, true, false, true, true, true, false), "original UI consumption is respected");
    Check(state.Accept('a', false, true, true, true, true, true, false), "still tracked only original accepted edges");
    state.Cancel();
    Check(!state.Armed() && !state.keys['a'].down && !state.TakeRetained('a'), "focus/escape cancels retained state");

    state = {};
    state.Begin(); state.Begin(); Down(state, 'q'); state.End();
    Check(state.Armed() && state.Accept('q', false, true, true, true, true, true, false), "nested save/load transition");
    state.End(); state.FirstCommandFinished(); state.FirstCommandFinished();
    Check(!state.Armed(), "nested transition ends once at resume");
    Check(!state.Accept(-1, false, true, true, true, true, true, false) &&
          !state.Accept(256, false, true, true, true, true, true, false), "invalid key codes ignored");
    ClientButton forward{{0, 0}, 0}, left{{0, 0}, 0};
    Check(RestoreHeldButton(forward, 'w', true) && RestoreHeldButton(left, 'a', true), "restore two cleared action states independently");
    Check(forward.state == 1 && left.state == 1, "continuous holds have no false half-speed press impulse");
    forward.state = 7;
    Check(RestoreHeldButton(forward, 'w', true) && forward.state == 1, "remove synthetic up/down impulses only for continuously held action");
    ClientButton fresh{{0, 0}, 0};
    Check(RestoreHeldButton(fresh, 'w', false) && fresh.state == 3, "real new press retains its impulse");
    fresh = {{'w', 128}, 1};
    Check(!RestoreHeldButton(fresh, 'a', true) && fresh.down[0] == 'w' && fresh.down[1] == 128, "never overwrite two other key owners");
    ClientButton duringLoadA{{0,0},0}, duringLoadW{{0,0},0};
    Check(RestoreHeldButton(duringLoadA,'a',false) && RestoreHeldButton(duringLoadW,'w',false), "new physical presses without client down callbacks restored independently");
    Check(duringLoadA.state == 3 && duringLoadW.state == 3, "first resumed command retains genuine fresh press impulses");
    duringLoadA = {{0,0},4}; // Delayed client reset, as observed in the trace.
    Check(RestoreHeldButton(duringLoadA,'a',true) && duringLoadA.state == 1, "late clear after first sample does not invent another press impulse");
    Check(ReleaseButtonOwner(duringLoadA,'a') && duringLoadA.state == 4, "physical release removes repaired owner even without client callback");
    Check(ReleaseButtonOwner(fresh,'w') && fresh.down[1] == 128 && fresh.state == 1, "release preserves a second owner of the same action");
    ClientButton repaired{{'w',0},1}, rebound{{'w',0},1};
    RepairedButtonOwner owner;
    owner.Remember(repaired,'w');
    LoadKeyState shortGuard; shortGuard.Begin(); shortGuard.End();
    shortGuard.FirstCommandFinished(); shortGuard.FirstCommandFinished();
    Check(!shortGuard.Armed() && owner.Active(), "repair ownership persists after the short resume guard");
    Check(owner.Release('w') && repaired.state == 4 && !owner.Active(), "late physical release without callback clears a repaired owner");
    repaired = {{'w',0},1}; owner.Remember(repaired,'w'); owner.Remember(rebound,'w');
    Check(repaired.state == 4 && rebound.state == 1, "changing repaired action releases its previous owner");
    Check(owner.Release('w') && !owner.Release('w'), "repair release is idempotent");
    std::cout << "Load-key state tests passed; physical game behavior still needs a focused playtest.\n";
}
