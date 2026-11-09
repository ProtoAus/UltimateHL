#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

// Used only on the game's window/input thread. Menu/focus transitions discard
// movement; they must never become a camera jump when gameplay resumes.
class RawMotion {
public:
    void SetActive(bool active) {
        if (active_ != active) {
            Clear();
            active_ = active;
        }
    }

    bool Active() const { return active_; }
    void Clear() { x_ = y_ = 0; }

    void Add(std::int32_t x, std::int32_t y) {
        if (!active_) return;
        // Bound each accumulation so neither arithmetic nor POINT conversion
        // can overflow, even if the game spends a long time in a loading call.
        constexpr std::int64_t limit = std::numeric_limits<std::int32_t>::max() / 4;
        x_ = std::clamp(x_ + x, -limit, limit);
        y_ = std::clamp(y_ + y, -limit, limit);
    }

    std::pair<std::int32_t, std::int32_t> Take() {
        auto result = std::make_pair(static_cast<std::int32_t>(x_),
                                     static_cast<std::int32_t>(y_));
        Clear();
        return result;
    }

private:
    bool active_ = false;
    std::int64_t x_ = 0;
    std::int64_t y_ = 0;
};
