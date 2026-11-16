// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/profile_avatar.hpp"

#include "ui/theme/theme.hpp"
#include "ui/widgets/icons.hpp"

#include <algorithm>

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr float kGrow = 1.12f;   // focused size (design 13)
constexpr float kNameGap = 26;
constexpr float kNameH = 44;
}  // namespace

ProfileAvatar::ProfileAvatar(std::string name, std::string initials, int colour, ClickHandler on_click, float size)
    : name_(std::move(name)), initials_(std::move(initials)), colour_(colour), on_click_(std::move(on_click)), size_(size) {
    set_focusable(true);
    set_focus_ring(false);
    grow_.start(1, 1, 0, 0);
}

ProfileAvatar &ProfileAvatar::set_badge(Badge b) {
    if (b == badge_) return *this;
    badge_ = b;
    if (badge_icon_) remove_child(badge_icon_);
    badge_icon_ = nullptr;
    if (b != Badge::None) {
        badge_icon_ = &add<IconView>(icon_url(b == Badge::Lock ? "lock" : "settings"));
    }
    invalidate();
    return *this;
}

ProfileAvatar &ProfileAvatar::set_current(bool on) {
    current_ = on;
    return *this;
}

ProfileAvatar &ProfileAvatar::set_name(std::string name) {
    name_ = std::move(name);
    return *this;
}

std::string ProfileAvatar::accessible_text() const {
    std::string t = name_;
    if (badge_ == Badge::Lock) t += ", locked";
    return t;
}

wnk::Size ProfileAvatar::measure_content(Ui &, wnk::Size) {
    return {size_ * kGrow + 20, size_ * kGrow + kNameGap + kNameH};
}

bool ProfileAvatar::on_action(Ui &ui, const wnk::input::ActionEvent &e) {
    if (e.action != wnk::input::Action::Confirm || !on_click_) return false;
    if (e.phase == wnk::input::Phase::Press) on_click_(ui);
    return true;
}

void ProfileAvatar::on_focus_changed(Ui &ui, bool focused) {
    View::on_focus_changed(ui, focused);
    grow_.start(grow_.value(ui.now_ms()), focused ? kGrow : 1.0f, ui.now_ms(), 180);
}

void ProfileAvatar::layout_content(Ui &ui, const wnk::Rect &c) {
    if (!badge_icon_) return;
    // Bottom right of the avatar, following its focus growth; the disc under it is drawn in draw().
    const float s = grow_.value(ui.now_ms());
    const float cx = c.x + c.w * 0.5f, cy = c.y + size_ * kGrow * 0.5f;
    const float b = size_ * 0.28f;
    const float x = cx + size_ * s * 0.5f - b * 0.8f, y = cy + size_ * s * 0.5f - b * 0.8f;
    badge_icon_->layout(ui, {x + b * 0.25f, y + b * 0.25f, b * 0.5f, b * 0.5f});
}

void ProfileAvatar::draw(Ui &ui) {
    auto &r = ui.renderer();
    const wnk::Rect f = frame();
    const bool focused = ui.is_focused(this);
    const float s = grow_.value(ui.now_ms());
    const float cx = f.x + f.w * 0.5f;
    const float cy = f.y + size_ * kGrow * 0.5f;
    const float d = size_ * s, rad = d * 0.5f;
    if (focused) {
        for (int i = 3; i >= 1; i--) {  // lifted: a soft shadow below
            const float g = 6.0f * i;
            r.fill_rect({cx - rad - g, cy - rad - g + 18, d + 2 * g, d + 2 * g}, ui.fade(theme::Color::hex(0x00000026)), rad + g);
        }
        r.fill_rect({cx - rad - 6, cy - rad - 6, d + 12, d + 12}, ui.fade(theme::kFocusFill), rad + 6);
    }
    const wnk::Rect disc{cx - rad, cy - rad, d, d};
    if (colour_ < 0) {
        r.fill_rect(disc, ui.fade(focused ? theme::Color::hex(0x2A3040FF) : theme::Color::hex(0x1A1E28FF)), rad);
        r.stroke_rect(disc, ui.fade(theme::Color::hex(0xFFFFFF4D)), 2, rad);
    } else {
        r.fill_rect(disc, ui.fade(theme::avatar_colour(colour_)), rad);
    }
    const wnk::render::TextStyle ts{ui.theme().bold, size_ * 0.36f * s, ui.fade(theme::kText)};
    const float tw = r.measure_text(ts, initials_);
    r.draw_text(ts, {cx - tw * 0.5f, cy - r.line_height(ts) * 0.5f + r.ascent(ts)}, initials_);
    if (badge_ != Badge::None) {
        if (!grow_.done(ui.now_ms()) || s != laid_out_scale_) {
            laid_out_scale_ = s;
            invalidate();  // the badge icon follows the growth
        }
        const float b = size_ * 0.28f;
        const float bx = cx + d * 0.5f - b * 0.8f, by = cy + d * 0.5f - b * 0.8f;
        r.fill_rect({bx - 4, by - 4, b + 8, b + 8}, ui.fade(theme::kBackground), b * 0.5f + 4);
        r.fill_rect({bx, by, b, b}, ui.fade(theme::Color::hex(0x232836FF)), b * 0.5f);
    }
    const wnk::render::TextStyle ns{ui.theme().bold, 30, ui.fade(focused ? theme::kText : theme::kTextSecondary)};
    const float nw = std::min(r.measure_text(ns, name_), f.w);
    const float ny = f.y + size_ * kGrow + kNameGap;
    r.draw_text(ns, {cx - nw * 0.5f, ny + r.ascent(ns)}, name_);
    if (current_) r.fill_rect({cx - 5, ny + kNameH, 10, 10}, ui.fade(theme::kAccent), 5);
}

}  // namespace csweb::ui
