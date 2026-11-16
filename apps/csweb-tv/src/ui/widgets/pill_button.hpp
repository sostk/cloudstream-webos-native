// SPDX-License-Identifier: GPL-3.0-or-later
//
// The designs' buttons: a fully rounded pill with an optional icon. Primary is white with dark text
// (the main action); Glass is a translucent pill that turns white when focused; Ghost has no fill
// until focused (navigation tabs). With no text it is a round icon button (give it an
// accessible_label). Focus is shown by the fill, not the kit's ring. A frosted Glass pill (over
// artwork: the top bar, hero buttons) shows the blurred artwork under it (glass.hpp). A pill is never
// wider than the room it is given: text that does not fit ends in an ellipsis, and a suffix (an
// episode's "· 100 %") is kept whole after it.
#pragma once

#include "ui/widgets/glass.hpp"
#include "ui/widgets/icons.hpp"

#include "wnk/ui/widgets.hpp"

#include <string>

namespace csweb::ui {

class PillButton : public wnk::ui::Button {
 public:
    enum class Style { Primary, Glass, Ghost };
    PillButton(std::string text, std::string icon, Style style, wnk::ui::ClickHandler on_click = {}, float height = 64);
    PillButton &set_selected(bool on);  // Ghost tabs: the section shown, marked when not focused
    PillButton &set_icon(std::string icon);
    PillButton &set_frosted(bool on);
    PillButton &set_suffix(std::string suffix);  // drawn after the text, never cut
    bool selected() const { return selected_; }

 protected:
    wnk::Size measure_content(wnk::ui::Ui &ui, wnk::Size available) override;
    void layout_content(wnk::ui::Ui &ui, const wnk::Rect &content) override;
    void update(wnk::ui::Ui &ui, float dt) override;
    void draw(wnk::ui::Ui &ui) override;

 private:
    float text_size() const;
    Style style_;
    std::string icon_;
    std::string suffix_;
    float height_;
    bool selected_ = false;
    bool shown_dark_ = false;
    bool frosted_ = false;
    glass::Surface surface_;
    IconView *icon_light_ = nullptr;
    IconView *icon_dark_ = nullptr;
};

}  // namespace csweb::ui
