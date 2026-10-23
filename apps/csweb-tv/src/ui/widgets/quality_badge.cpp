// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/quality_badge.hpp"

#include "ui/theme/theme.hpp"

namespace csweb::ui {

QualityBadge::QualityBadge(std::string text, Kind kind, float font_size)
    : text_(std::move(text)), kind_(kind), font_size_(font_size) {
    set_focus_ring(false);
}

QualityBadge &QualityBadge::set_text(std::string text) {
    if (text_ != text) {
        text_ = std::move(text);
        invalidate();
    }
    return *this;
}

QualityBadge &QualityBadge::set_kind(Kind kind) {
    if (kind_ != kind) {
        kind_ = kind;
        invalidate();
    }
    return *this;
}

wnk::Size QualityBadge::measure_content(wnk::ui::Ui &ui, wnk::Size) {
    if (text_.empty()) return {0, 0};
    wnk::render::TextStyle s{ui.theme().bold, font_size_, theme::kText};
    const float text_w = ui.renderer().measure_text(s, text_);
    const float pad_x = font_size_ * 0.55f;
    const float pad_y = 5.0f;
    const float h = ui.renderer().line_height(s) + pad_y * 2.0f;
    return {text_w + pad_x * 2.0f, h};
}

void QualityBadge::layout_content(wnk::ui::Ui &, const wnk::Rect &) {}

void QualityBadge::draw(wnk::ui::Ui &ui) {
    if (text_.empty()) return;
    auto &r = ui.renderer();
    wnk::render::Color bg_color;
    wnk::render::Color border_color;
    wnk::render::Color text_color;

    switch (kind_) {
        case Kind::Quality4K:
            bg_color = wnk::render::Color::hex(0xE5A93C2A);
            border_color = wnk::render::Color::hex(0xF6C453DD);
            text_color = wnk::render::Color::hex(0xFFE28AFF);
            break;
        case Kind::Quality1080p:
            bg_color = wnk::render::Color::hex(0x1F3A6055);
            border_color = wnk::render::Color::hex(0x7FB2FFEE);
            text_color = wnk::render::Color::hex(0xD5E6FFFF);
            break;
        case Kind::Quality720p:
            bg_color = wnk::render::Color::hex(0x22263266);
            border_color = wnk::render::Color::hex(0xA7AEBB88);
            text_color = wnk::render::Color::hex(0xE2E6EDFF);
            break;
        case Kind::DolbyVision:
            bg_color = wnk::render::Color::hex(0x9E721633);
            border_color = wnk::render::Color::hex(0xE5B83CDD);
            text_color = wnk::render::Color::hex(0xFFE082FF);
            break;
        case Kind::Hdr:
            bg_color = wnk::render::Color::hex(0x1C4A7E44);
            border_color = wnk::render::Color::hex(0x60A5FADD);
            text_color = wnk::render::Color::hex(0xBFDBFEFF);
            break;
        case Kind::DolbyAtmos:
            bg_color = wnk::render::Color::hex(0x1A284044);
            border_color = wnk::render::Color::hex(0x6EE7B7DD);
            text_color = wnk::render::Color::hex(0xD1FAE5FF);
            break;
        case Kind::QualityGeneric:
        default:
            bg_color = wnk::render::Color::hex(0x161921AA);
            border_color = wnk::render::Color::hex(0xFFFFFF33);
            text_color = wnk::render::Color::hex(0xF4F5F7FF);
            break;
    }

    const float radius = 6.0f;
    const float border_width = 1.5f;
    const auto &f = frame();

    r.fill_rect(f, ui.fade(bg_color), radius);
    r.stroke_rect(f, ui.fade(border_color), border_width, radius);

    wnk::render::TextStyle s{ui.theme().bold, font_size_, ui.fade(text_color)};
    const float text_w = r.measure_text(s, text_);
    const float x = f.x + (f.w - text_w) * 0.5f;
    const float y = f.y + (f.h - r.line_height(s)) * 0.5f + r.ascent(s);
    r.draw_text(s, {x, y}, text_);
}

}  // namespace csweb::ui
