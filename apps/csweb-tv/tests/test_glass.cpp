// SPDX-License-Identifier: GPL-3.0-or-later
// ui/widgets/glass: the blurred artwork copy and a glass panel's composed pixels.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "ui/widgets/glass.hpp"

using namespace csweb::ui;

namespace {
const uint8_t *at(const std::vector<uint8_t> &px, int w, int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; }

// Left half red, right half blue.
wnk::image::ImageRGBA halves(int w, int h) {
    wnk::image::ImageRGBA img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t *p = &img.pixels[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = x < w / 2 ? 255 : 0;
            p[2] = x < w / 2 ? 0 : 255;
            p[3] = 255;
        }
    }
    return img;
}
}  // namespace

TEST("blur_scales_down_and_softens_edges") {
    const glass::Blurred b = glass::blur(halves(640, 360), 160, 3);
    CHECK_EQ(b.width, 160);
    CHECK_EQ(b.height, 90);
    CHECK(b.id != 0);
    CHECK(glass::blur(halves(640, 360)).id != b.id);
    const uint8_t *edge = &b.rgba[(45 * 160 + 80) * 4];
    CHECK(edge[0] > 40 && edge[0] < 215);  // the hard edge became a ramp
    CHECK(edge[2] > 40 && edge[2] < 215);
    CHECK_EQ(b.rgba[(45 * 160 + 2) * 4 + 0], 255);  // far from the edge: unchanged
    CHECK_EQ(b.rgba[3], 255);
    CHECK_EQ(glass::blur(wnk::image::ImageRGBA{}).width, 0);
}

TEST("panel_is_rounded_with_a_brighter_top_border") {
    const glass::Style s = glass::panel_style();
    const int w = 200, h = 60;
    const auto px = glass::compose(w, h, 30, s, nullptr, {0, 0, 200, 60});
    CHECK_EQ(at(px, w, 0, 0)[3], 0);       // outside the rounded corner
    CHECK_EQ(at(px, w, w - 1, h - 1)[3], 0);
    CHECK(at(px, w, 100, 30)[3] > 200);     // the body: translucent ground
    CHECK(at(px, w, 100, 30)[3] < 255);
    CHECK(at(px, w, 100, 0)[0] > at(px, w, 100, h - 1)[0]);  // border brighter at the top
}

TEST("panel_over_artwork_shows_the_blurred_colour_under_it") {
    glass::Under under;
    under.image = std::make_shared<const glass::Blurred>(glass::blur(halves(640, 360)));
    under.frame = {0, 0, 1920, 1080};
    const glass::Style s = glass::chip_style();
    const auto left = glass::compose(100, 50, 25, s, &under, {100, 500, 100, 50});
    const auto right = glass::compose(100, 50, 25, s, &under, {1700, 500, 100, 50});
    CHECK_EQ(at(left, 100, 50, 25)[3], 255);  // opaque over artwork
    CHECK(at(left, 100, 50, 25)[0] > at(left, 100, 50, 25)[2]);    // red under the left panel
    CHECK(at(right, 100, 50, 25)[2] > at(right, 100, 50, 25)[0]);  // blue under the right one
    under.left_strength = 1;
    under.left_extent = 0.5f;
    const auto scrim = glass::compose(100, 50, 25, s, &under, {0, 500, 100, 50});
    CHECK(at(scrim, 100, 50, 25)[0] < at(left, 100, 50, 25)[0]);  // the backdrop's scrim darkens it
}

TEST("a smaller texture covers the whole panel") {
    // A dialog's glass is composed at a quarter of its size: pixels still map across the full panel.
    glass::Under under;
    under.image = std::make_shared<const glass::Blurred>(glass::blur(halves(640, 360)));
    under.frame = {0, 0, 1920, 1080};
    const glass::Style s = glass::dialog_style();
    CHECK(s.resolution < 1);
    // 1600 logical units wide from x=160: its left end is over red, its right end over blue.
    const int w = 400, h = 100;
    const auto px = glass::compose(w, h, 6, s, &under, {160, 400, 1600, 400});
    CHECK(at(px, w, 20, 50)[0] > at(px, w, 20, 50)[2]);
    CHECK(at(px, w, w - 20, 50)[2] > at(px, w, w - 20, 50)[0]);
    CHECK_EQ(at(px, w, w / 2, 50)[3], 255);
    CHECK_EQ(at(px, w, 0, 0)[3], 0);  // rounded corner
}

namespace {
struct Scrolled : wnk::ui::View {
    wnk::Point by;
    wnk::Point scroll_offset() const override { return by; }
};
}  // namespace

TEST("scroll_shift: the scroll offsets of every view above, not the view's own") {
    Scrolled outer;
    outer.by = {0, 300};
    auto &mid = outer.add<Scrolled>();
    mid.by = {0, 0};
    auto &in = mid.add_child(std::make_unique<Scrolled>());
    static_cast<Scrolled &>(in).by = {40, 0};  // its own offset moves its children, not itself
    auto &leaf = in.add<wnk::ui::View>();
    const wnk::Point s = glass::scroll_shift(leaf);
    CHECK_EQ(s.x, -40.0f);
    CHECK_EQ(s.y, -300.0f);
    const wnk::Point own = glass::scroll_shift(in);
    CHECK_EQ(own.x, 0.0f);
    CHECK_EQ(own.y, -300.0f);
    CHECK_EQ(glass::scroll_shift(outer).y, 0.0f);
}

TEST("fade_bottom: a soft lower edge instead of a line") {
    glass::Style s = glass::panel_style();
    s.border_top = s.border_bottom = 0;
    s.fade_bottom = 0.5f;
    const int w = 40, h = 100;
    const auto px = glass::compose(w, h, 0, s, nullptr, {0, 0, 40, 100});
    const int top = at(px, w, 20, 10)[3], upper = at(px, w, 20, 45)[3], mid = at(px, w, 20, 75)[3], low = at(px, w, 20, 99)[3];
    CHECK(top > 200);         // the upper half keeps the full ground
    CHECK_EQ(upper, top);
    CHECK(mid < top && mid > low);  // then it eases out
    CHECK(low < 5);                 // to nothing at the bottom
}
