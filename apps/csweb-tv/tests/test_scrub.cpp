// SPDX-License-Identifier: GPL-3.0-or-later
// player/scrub against Media3 DefaultTimeBar's defaults (a twentieth of the duration per press,
// the seek a second after the last press).
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "player/scrub.hpp"

using csweb::player::Scrub;

TEST("each press moves a twentieth of the duration from where the bar stood") {
    Scrub s;
    const int64_t duration = 8400000000;  // 2:20:00
    CHECK(s.step(+1, 821000000, duration, 0));
    CHECK_EQ(s.target_us(), 821000000 + 420000000);
    CHECK(s.step(+1, 0, duration, 100));  // the position no longer matters once scrubbing
    CHECK_EQ(s.target_us(), 821000000 + 840000000);
    CHECK(s.step(-1, 0, duration, 200));
    CHECK_EQ(s.target_us(), 821000000 + 420000000);
}

TEST("the target stays within the media") {
    Scrub s;
    CHECK(s.step(-1, 3000000, 100000000, 0));  // 3 s back by 5 s
    CHECK_EQ(s.target_us(), 0);
    CHECK(s.step(+1, 0, 100000000, 0));
    for (int i = 0; i < 30; ++i) s.step(+1, 0, 100000000, 0);
    CHECK_EQ(s.target_us(), 100000000);
}

TEST("the seek comes a second after the last press, or at once on OK") {
    Scrub s;
    s.step(+1, 0, 100000000, 1000);
    s.step(+1, 0, 100000000, 1500);
    CHECK_EQ(s.finish(2400), Scrub::kNone);  // 0.9 s after the last press
    CHECK(s.active());
    CHECK_EQ(s.finish(2500), 10000000);
    CHECK(!s.active());
    CHECK_EQ(s.finish(9000), Scrub::kNone);  // once
    s.step(-1, 50000000, 100000000, 3000);
    CHECK_EQ(s.finish(3001, /*now=*/true), 45000000);
}

TEST("nothing to scrub without a duration (live)") {
    Scrub s;
    CHECK(!s.step(+1, 5000000, Scrub::kNone, 0));
    CHECK(!s.step(+1, 5000000, 0, 0));
    CHECK(!s.active());
    CHECK_EQ(s.target_us(), Scrub::kNone);
}
