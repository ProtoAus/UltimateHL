#include "../native/load_fps_guard.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
int main() {
    LoadFpsGuard guard;
    guard.Begin(4, 0, 1);
    Check(!guard.changed && !guard.Restore(4, 1), "default disabled preserves stock FPS");
    guard.Begin(4, 100, 2);
    Check(guard.changed && guard.applied == 100 && guard.before == 4, "raise loading-only cap");
    Check(guard.Restore(100, 2), "restore untouched temporary cap after return");
    Check(!guard.Restore(60, 2), "don't overwrite observed external FPS change");
    Check(!guard.Restore(100, 3), "explicit selection wins even when it equals the floor");
    guard.Begin(100, 60, 3);
    Check(!guard.changed, "never lower an existing cap");
    for (float invalid : {0.0f, -1.0f, 101.0f, 19.0f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        guard.Begin(4, invalid, 0);
        Check(!guard.changed, "invalid minimum has no effect");
    }
    guard.Begin(std::numeric_limits<float>::quiet_NaN(), 100, 0);
    Check(!guard.changed, "invalid current FPS is not changed");
    LoadFpsSession session;
    session.Begin(4);Check(session.Effective(100)==100,"floor begins at load");session.Applied(100);
    Check(session.active && session.Effective(100)==100,"handler return alone does not end floor");
    session.Select(4);Check(session.Effective(100)==100,"in-load 4 FPS choice does not slow the load");session.Applied(100);
    session.Begin(100);Check(session.desired==4,"nested/repeated transition does not snapshot the temporary cap");
    Check(session.Finish()==4 && !session.active,"gameplay resumes at selected FPS");
    session.Begin(4);session.Select(100);Check(session.Finish()==100,"new gameplay 100 choice persists");
    session.Begin(4);session.Applied(100);session.Observe(30);Check(session.desired==30 && session.Effective(100)==100,"external change saved while floor maintained");
    Check(session.Effective(0)==30 && session.Effective(19)==30,"disabled/invalid floor respected");
    float choice=0;
    Check(ParseFpsBinding("uhl_fps 4",choice) && choice==4,"literal low-FPS binding");
    Check(ParseFpsBinding("  uhl_fps 100  ",choice) && choice==100,"literal high-FPS binding");
    for (const char* text : {"uhl_fps 0","uhl_fps nan","uhl_fps 101","uhl_fps 100;quit","alias_name","fps_max 4","uhl_fps"})
        Check(!ParseFpsBinding(text,choice),"never execute arbitrary/invalid bindings from a window message");
    std::cout << "Loading-FPS decision tests passed (not a live-engine timing test).\n";
}
