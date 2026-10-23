// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wnk/render/color.hpp"
#include "wnk/ui/ui.hpp"
#include "wnk/ui/view.hpp"

#include <string>

namespace csweb::ui {

class QualityBadge : public wnk::ui::View {
 public:
    enum class Kind : uint8_t {
        Quality4K,
        Quality1080p,
        Quality720p,
        QualityGeneric,
        DolbyVision,
        Hdr,
        DolbyAtmos,
    };

    QualityBadge(std::string text, Kind kind, float font_size = 22.0f);

    QualityBadge &set_text(std::string text);
    QualityBadge &set_kind(Kind kind);

    const std::string &text() const { return text_; }
    Kind kind() const { return kind_; }

 protected:
    wnk::Size measure_content(wnk::ui::Ui &ui, wnk::Size available) override;
    void layout_content(wnk::ui::Ui &ui, const wnk::Rect &content) override;
    void draw(wnk::ui::Ui &ui) override;

 private:
    std::string text_;
    Kind kind_;
    float font_size_;
};

}  // namespace csweb::ui
