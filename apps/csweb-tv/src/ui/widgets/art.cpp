// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/art.hpp"

#include "ui/theme/theme.hpp"

#include <cmath>

namespace csweb::ui {

using namespace wnk::ui;

void draw_ramp(Ui &ui, const wnk::Rect &r, wnk::render::Color c, float a0, float a1, bool horizontal, int steps) {
    auto &rr = ui.renderer();
    for (int i = 0; i < steps; i++) {
        const float t = (i + 0.5f) / steps;
        const float a = a0 + (a1 - a0) * t;
        if (a <= 0.003f) continue;
        // Whole-pixel edges shared by neighbours: overlapping bands would draw darker seams.
        wnk::Rect band = r;
        if (horizontal) {
            band.x = std::floor(r.x + r.w * i / steps);
            band.w = std::floor(r.x + r.w * (i + 1) / steps) - band.x;
        } else {
            band.y = std::floor(r.y + r.h * i / steps);
            band.h = std::floor(r.y + r.h * (i + 1) / steps) - band.y;
        }
        rr.fill_rect(band, ui.fade(c.with_alpha(a)));
    }
}

Backdrop::Backdrop(std::string url) {
    image_ = &add<ImageView>(url);
    image_->set_fit(ImageFit::Cover);
    image_->set_visible(!url.empty());  // no artwork: the screen background, not a placeholder
    blur_.set_url(url);
}

void Backdrop::set_url(const std::string &url) {
    if (image_->url() != url) image_->set_url(url);
    image_->set_visible(!url.empty());
    blur_.set_url(url);
}

void Backdrop::layout_content(Ui &ui, const wnk::Rect &content) { image_->layout(ui, content); }

void Backdrop::draw_over(Ui &ui) {
    const wnk::Rect f = frame();
    draw_ramp(ui, {f.x, f.y, f.w * left_extent, f.h}, theme::kBackground, left_strength, 0.0f, true);
    const float h = f.h * bottom_extent;
    draw_ramp(ui, {f.x, f.y + f.h - h, f.w, h}, theme::kBackground, 0.0f, fade_to_background ? 1.0f : 0.8f, false);
    if (image_->visible() && image_->state() == ImageView::State::Ready && blur_.image()) {
        const wnk::Point at = glass::scroll_shift(*this);
        glass::publish(ui, {blur_.image(), {f.x + at.x, f.y + at.y, f.w, f.h}, left_strength, left_extent, bottom_extent,
                            fade_to_background});
    }
}

}  // namespace csweb::ui
