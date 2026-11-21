// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/top_bar.hpp"

#include "profiles/profiles.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/pill_button.hpp"

#include <algorithm>

namespace csweb::ui {

using namespace wnk::ui;
using Tab = app::Router::Tab;

TopBar::TopBar(app::Router &router, Tab current) : Row(0), router_(router) {
    set_padding({theme::kSafeX, theme::kSafeTop, theme::kSafeX, 0});
    set_align(Align::Stretch, Align::Start);

    auto &left = add<Row>(0);
    left.set_width(Dim::fill());
    left.set_align(Align::Start, Align::Center);
    left.set_gap(14);
    avatar_ = &left.add<Label>("", TextRole::Body);
    avatar_->set_bold(true).set_text_align(Align::Center);
    avatar_->set_size(72, 72);
    avatar_->set_padding({0, 18, 0, 0});
    // The profile's name ("Profiles" while there is only the default one); opens the picker.
    who_ = &left.add<PillButton>("", "", PillButton::Style::Ghost, [&router](Ui &ui) { router.open_profiles(ui); }, 56);
    who_->name = "nav.profile";
    who_->accessible_label = "Profiles";
    profile_sub_ = router.services().profile_changed.subscribe([this] { show_profile(); });
    show_profile();

    auto &nav = add<NavBar>();
    struct Item {
        const char *text, *icon;
        Tab tab;
    };
    const Item items[] = {{"Home", "", Tab::Home},
                          {"Search", "search", Tab::Search},
                          {"Library", "bookmark", Tab::Library},
                          {"Settings", "settings", Tab::Settings}};
    for (const auto &it : items) {
        const Tab tab = it.tab;
        auto &b = nav.add_tab(it.text, it.icon, [&router, tab](Ui &ui) { router.open_tab(ui, tab); });
        b.name = std::string("nav.") + it.text;
        if (tab == current) current_tab_ = &b;
    }
    nav.set_selected(static_cast<size_t>(current));

    right_ = &add<Row>(20);
    right_->set_width(Dim::fill());
    right_->set_justify(Align::End);
    right_->set_align(Align::Stretch, Align::Center);
}

namespace {
constexpr float kBackingBelow = 90;  // the strip reaches this far below the bar's controls, fading out
constexpr float kBackingFade = 120;  // scrolled this far, the strip is fully shown

glass::Style backing_style() {
    // The navigation pill's glass, as a full-width strip: no shadow and no border, its lower part
    // fading out so there is no hard line under the bar; composed at a quarter of the size.
    glass::Style s = glass::panel_style();
    s.tint_alpha = 0.62f;
    s.ground_alpha = 0.9f;
    s.shadow = false;
    s.border_top = s.border_bottom = 0;
    s.highlight = 0.04f;
    s.fade_bottom = 0.5f;
    s.resolution = 0.25f;
    return s;
}
}  // namespace

void TopBar::draw(Ui &ui) {
    if (scroller_ == nullptr) return;
    const float shown = std::clamp(scroller_->scroll_offset().y / kBackingFade, 0.0f, 1.0f);
    if (shown <= 0) return;
    const wnk::Rect f = frame();
    // A little wider than the screen so the side borders fall outside it.
    backing_.draw(ui, {f.x - 2, f.y - 2, f.w + 4, f.h + kBackingBelow + 2}, 0, backing_style(), shown, {},
                  glass::scroll_shift(*this));
}

void TopBar::show_profile() {
    const auto &s = router_.services();
    const profiles::Profile &p = s.profiles.current();
    avatar_->set_text(profiles::initials(p.name));
    avatar_->set_background(theme::avatar_colour(p.image_index), 36);
    who_->set_text(s.profiles.all().size() > 1 ? p.name : "Profiles");
}

}  // namespace csweb::ui
