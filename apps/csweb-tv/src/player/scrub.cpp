// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/scrub.hpp"

#include <algorithm>

namespace csweb::player {

bool Scrub::step(int direction, int64_t position_us, int64_t duration_us, int64_t now_ms) {
    if (duration_us <= 0 || duration_us == kNone) return false;
    if (!active_) {
        active_ = true;
        target_us_ = position_us == kNone ? 0 : position_us;
    }
    const int64_t increment = duration_us / kIncrementCount;
    target_us_ = std::clamp<int64_t>(target_us_ + (direction < 0 ? -increment : increment), 0, duration_us);
    last_ms_ = now_ms;
    return true;
}

int64_t Scrub::finish(int64_t now_ms, bool now) {
    if (!active_ || (!now && now_ms - last_ms_ < kStopAfterMs)) return kNone;
    active_ = false;
    return target_us_;
}

}  // namespace csweb::player
