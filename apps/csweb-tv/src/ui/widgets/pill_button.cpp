// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/pill_button.hpp"

#include "ui/theme/theme.hpp"
#include "ui/widgets/icons.hpp"

#include <algorithm>

namespace csweb::ui {

using namespace wnk::ui;

PillButton::PillButton(std::string text, std::string icon, Style style, ClickHandler on_click, float height)
    : Button(std::move(text), std::move(on_click)), style_(style), height_(height) {
    set_focus_ring(false);
    set_padding({});  // the pill's own insets are part of its measured size (Button pads otherwise)
    set_icon(std::move(icon));
    if (this->text().empty() && accessible_label.empty()) accessible_label = icon_;
}

PillButton &PillButton::set_selected(bool on) {
    selected_ = on;
    return *this;
}

PillButton &PillButton::set_frosted(bool on) {
    frosted_ = on;
    return *this;
}

PillButton &PillButton::set_suffix(std::string suffix) {
    suffix_ = std::move(suffix);
    invalidate();
    return *this;
}

PillButton &PillButton::set_icon(std::string icon) {
    if (icon == icon_ && (icon_.empty() || icon_light_)) return *this;  // callers may set it every frame
    icon_ = std::move(icon);
    if (icon_light_) remove_child(icon_light_);
    if (icon_dark_) remove_child(icon_dark_);
    icon_light_ = icon_dark_ = nullptr;
    shown_dark_ = false;  // new views start light: update() picks the variant again
    if (!icon_.empty()) {
        icon_light_ = &add<IconView>(icon_url(icon_));
        icon_dark_ = &add<IconView>(icon_url(icon_, true));
        icon_dark_->set_visible(false);
    }
    invalidate();
    return *this;
}

float PillButton::text_size() const { return height_ >= 64 ? theme::kLabel + 2 : theme::kCaption; }

wnk::Size PillButton::measure_content(Ui &ui, wnk::Size available) {
    const float icon = icon_.empty() ? 0 : height_ * 0.42f;
    if (text().empty()) return {height_, height_};
    const wnk::render::TextStyle s{ui.theme().bold, text_size(), theme::kText};
    const float pad = height_ * 0.47f;
    const float gap = icon > 0 ? 12 : 0;
    const float natural = pad * 2 + icon + gap + ui.renderer().measure_text(s, text() + suffix_);
    // No wider than the room given (a dialog's content width); draw() ends the text in an ellipsis.
    // A width under two heights is a measuring pass without room, not a limit.
    const bool limited = available.w >= height_ * 2 && natural > available.w;
    return {limited ? available.w : natural, height_};
}

void PillButton::layout_content(Ui &ui, const wnk::Rect &content) {
    if (icon_.empty()) return;
    const float icon = height_ * 0.42f;
    float x;
    if (text().empty()) {
        x = content.x + (content.w - icon) * 0.5f;
    } else {
        x = content.x + height_ * 0.47f;
    }
    const wnk::Rect r{x, content.y + (content.h - icon) * 0.5f, icon, icon};
    icon_light_->layout(ui, r);
    icon_dark_->layout(ui, r);
}

void PillButton::update(Ui &ui, float dt) {
    Button::update(ui, dt);
    const bool focused = ui.is_focused(this);
    const bool dark = focused || style_ == Style::Primary;
    if (icon_light_ && dark != shown_dark_) {
        shown_dark_ = dark;
        icon_light_->set_visible(!dark);
        icon_dark_->set_visible(dark);
    }
}

void PillButton::draw(Ui &ui) {
    auto &r = ui.renderer();
    const wnk::Rect f = frame();
    const bool focused = ui.is_focused(this);
    const float radius = f.h * 0.5f;
    if (style_ == Style::Primary) {
        if (focused) r.fill_rect({f.x - 6, f.y - 6, f.w + 12, f.h + 12}, ui.fade(theme::kFocusFill.with_alpha(0.3f)), radius + 6);
        r.fill_rect(f, ui.fade(theme::kFocusFill), radius);
    } else if (focused) {
        r.fill_rect(f, ui.fade(theme::kFocusFill), radius);
    } else if (style_ == Style::Glass && frosted_) {
        surface_.draw(ui, f, radius, glass::chip_style(), 1, {}, glass::scroll_shift(*this));
    } else if (style_ == Style::Glass) {
        r.fill_rect(f, ui.fade(theme::kGlassBorder), radius);
        r.fill_rect({f.x + 1, f.y + 1, f.w - 2, f.h - 2}, ui.fade(theme::Color::hex(0x1E2129FF)), radius - 1);
    } else if (selected_) {
        r.fill_rect(f, ui.fade(theme::kSurfaceRaised), radius);
    }
    if (text().empty()) return;
    const bool dark = focused || style_ == Style::Primary;
    const wnk::render::TextStyle s{ui.theme().bold, text_size(),
                                   ui.fade(!enabled() ? theme::kTextMuted : dark ? theme::kOnFocus : theme::kText)};
    const float icon = icon_.empty() ? 0 : height_ * 0.42f + 12;
    const float lh = r.line_height(s);
    const float pad = height_ * 0.47f;
    const float suffix_w = suffix_.empty() ? 0 : r.measure_text(s, suffix_);
    const float room = f.w - pad * 2 - icon - suffix_w;
    std::string shown = text();
    if (r.measure_text(s, shown) > room + 0.5f) {  // text that fits exactly is not cut by rounding
        const auto lines = Label::wrap(r, s, shown, std::max(room, 0.0f), 1);
        shown = lines.empty() ? std::string() : lines.front();
    }
    const float baseline = f.y + (f.h - lh) * 0.5f + r.ascent(s);
    const float x = f.x + pad + icon;
    const float advance = r.draw_text(s, {x, baseline}, shown);
    if (!suffix_.empty()) r.draw_text(s, {x + advance, baseline}, suffix_);
}

}  // namespace csweb::ui
