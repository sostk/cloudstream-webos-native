// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/nav_bar.hpp"

#include "ui/theme/theme.hpp"

namespace csweb::ui {

using namespace wnk::ui;

NavBar::NavBar() : Row(4) {
    set_padding(Insets::all(6));
    set_focus_scope(false, true);
    set_align(Align::Center, Align::Start);
}

PillButton &NavBar::add_tab(std::string text, std::string icon, ClickHandler on_click) {
    auto &b = add<PillButton>(std::move(text), std::move(icon), PillButton::Style::Ghost, std::move(on_click));
    tabs_.push_back(&b);
    return b;
}

void NavBar::set_selected(size_t index) {
    for (size_t i = 0; i < tabs_.size(); i++) tabs_[i]->set_selected(i == index);
}

void NavBar::draw(Ui &ui) {
    const wnk::Rect f = frame();
    surface_.draw(ui, f, f.h * 0.5f, glass::panel_style(), 1, {}, glass::scroll_shift(*this));
}

}  // namespace csweb::ui
