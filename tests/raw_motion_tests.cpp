#include "../native/raw_motion.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
}

int main() {
    RawMotion motion;
    motion.Add(42, -51);
    Check(motion.Take() == std::make_pair(0, 0), "ignore inactive/menu motion");
    motion.SetActive(true);
    motion.Add(7, -9);
    motion.Add(-2, 3);
    Check(motion.Take() == std::make_pair(5, -6), "sum raw counts without scaling");
    Check(motion.Take() == std::make_pair(0, 0), "consume once; no double counting");
    motion.Add(11, 13);
    motion.SetActive(true);
    Check(motion.Take() == std::make_pair(11, 13), "polling active state preserves counts");
    motion.Add(100, 200);
    motion.SetActive(false);
    motion.SetActive(true);
    Check(motion.Take() == std::make_pair(0, 0), "discard on menu/focus transition");
    motion.Add(std::numeric_limits<int>::max(), std::numeric_limits<int>::min());
    const auto clamped = motion.Take();
    Check(clamped.first == std::numeric_limits<int>::max() / 4 &&
          clamped.second == -std::numeric_limits<int>::max() / 4, "bound extremes safely");
    motion.Add(1, 2);
    motion.Clear();
    Check(motion.Take() == std::make_pair(0, 0), "explicit clear");
    std::cout << "Raw-motion tests passed.\n";
}
