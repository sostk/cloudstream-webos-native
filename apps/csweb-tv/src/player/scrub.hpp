// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-pad scrubbing on the player's time bar, as Media3's DefaultTimeBar does it for CloudStream's
// LivePreviewTimeBar (CloudStream keeps the bar's defaults): each Left/Right press moves the target a
// twentieth of the duration from where the bar stands, the shown time follows the target, and the
// seek happens when the keys have stopped for a second, on OK, or when focus leaves the bar.
#pragma once

#include <cstdint>

namespace csweb::player {

class Scrub {
 public:
    static constexpr int kIncrementCount = 20;     // DefaultTimeBar.DEFAULT_INCREMENT_COUNT
    static constexpr int64_t kStopAfterMs = 1000;  // DefaultTimeBar.STOP_SCRUBBING_TIMEOUT_MS
    static constexpr int64_t kNone = INT64_MIN;    // wnk::media::kNoTime

    // A press towards `direction` (-1 back, +1 forward). The first one starts at `position_us`.
    // False when there is nothing to scrub (no known duration: live streams).
    bool step(int direction, int64_t position_us, int64_t duration_us, int64_t now_ms);
    bool active() const { return active_; }
    int64_t target_us() const { return active_ ? target_us_ : kNone; }
    // The target to seek to once the keys have stopped for kStopAfterMs, or at once with `now`; the
    // scrub ends. kNone while it goes on, or when there is none.
    int64_t finish(int64_t now_ms, bool now = false);

 private:
    bool active_ = false;
    int64_t target_us_ = 0;
    int64_t last_ms_ = 0;
};

}  // namespace csweb::player
