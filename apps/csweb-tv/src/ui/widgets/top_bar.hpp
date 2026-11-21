// SPDX-License-Identifier: GPL-3.0-or-later
//
// The top of every main screen (designs 01, 02, 09): the profile avatar on the left (it opens the
// profiles, as CloudStream's account button on TV), the glass
// navigation pill in the middle, and a slot on the right (the Home screen puts the provider picker
// and the clock there).
#pragma once

#include "app/router.hpp"
#include "ui/widgets/glass.hpp"
#include "ui/widgets/nav_bar.hpp"

#include "wnk/ui/widgets.hpp"

namespace csweb::ui {

class PillButton;

class TopBar : public wnk::ui::Row {
 public:
    TopBar(app::Router &router, app::Router::Tab current);
    wnk::ui::Row &right() { return *right_; }
    // The tab of the screen showing this bar (where focus starts on a tab screen).
    wnk::ui::View *current_tab() const { return current_tab_; }
    // The page scrolling under the bar: once it has scrolled, a strip of glass fades in behind the
    // bar so the rows passing under it do not show through its labels.
    void follow(const wnk::ui::View *scroller) { scroller_ = scroller; }

 protected:
    void draw(wnk::ui::Ui &ui) override;

 private:
    void show_profile();
    app::Router &router_;
    wnk::ui::Label *avatar_ = nullptr;
    PillButton *who_ = nullptr;
    wnk::ui::View *current_tab_ = nullptr;
    wnk::ui::Row *right_;
    core::Signal::Subscription profile_sub_;
    const wnk::ui::View *scroller_ = nullptr;
    glass::Surface backing_;
};

}  // namespace csweb::ui
