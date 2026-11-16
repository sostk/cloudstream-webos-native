// SPDX-License-Identifier: GPL-3.0-or-later
//
// A profile in the "Who's watching?" row (design 13): a round avatar in the profile's colour with
// its initials and its name under it. Focus grows the avatar, rings it in white and lifts it with a
// shadow. A badge marks a locked profile (lock) or the manage mode (pencil); the "add" tile is a
// translucent circle with a plus.
#pragma once

#include "ui/widgets/icons.hpp"

#include "wnk/ui/anim.hpp"
#include "wnk/ui/widgets.hpp"

#include <string>

namespace csweb::ui {

class ProfileAvatar : public wnk::ui::View {
 public:
    enum class Badge { None, Lock, Edit };
    // `colour` < 0: the "add" tile (no initials; `initials` is drawn as given, e.g. "+").
    ProfileAvatar(std::string name, std::string initials, int colour, wnk::ui::ClickHandler on_click, float size = 200);
    ProfileAvatar &set_badge(Badge b);
    ProfileAvatar &set_current(bool on);  // a small accent dot under the name
    ProfileAvatar &set_name(std::string name);
    std::string accessible_text() const override;

 protected:
    wnk::Size measure_content(wnk::ui::Ui &ui, wnk::Size available) override;
    bool on_action(wnk::ui::Ui &ui, const wnk::input::ActionEvent &e) override;
    void on_focus_changed(wnk::ui::Ui &ui, bool focused) override;
    void draw(wnk::ui::Ui &ui) override;
    void layout_content(wnk::ui::Ui &ui, const wnk::Rect &content) override;

 private:
    std::string name_, initials_;
    int colour_;
    wnk::ui::ClickHandler on_click_;
    float size_;
    Badge badge_ = Badge::None;
    bool current_ = false;
    wnk::ui::Tween grow_;
    float laid_out_scale_ = 1;
    IconView *badge_icon_ = nullptr;
};

}  // namespace csweb::ui
