// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/library_screen.hpp"

#include "settings/settings_store.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/glass_dialog.hpp"
#include "ui/widgets/pill_button.hpp"
#include "ui/widgets/top_bar.hpp"

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr int kColumns = 6;
std::vector<std::string> tab_titles(const library::Library &lib) {
    std::vector<std::string> out;
    for (library::WatchType t : library::watch_types()) {
        const size_t n = lib.bookmarks(t).size();
        out.push_back(std::string(library::watch_type_label(t)) + (n ? "  " + std::to_string(n) : std::string()));
    }
    return out;
}
}  // namespace

LibraryScreen::LibraryScreen(app::Services &s) : Screen("Library"), s_(s) {
    name = "library";
    auto &col = add<Column>(24);
    col.set_padding({theme::kSafeX, 170, theme::kSafeX, 40});
    col.set_width(Dim::fill()).set_height(Dim::fill());
    auto &head = col.add<wnk::ui::Row>(24);
    head.set_width(Dim::fill());
    tabs_ = &head.add<Tabs>(tab_titles(s_.library->get()), 0, [this](Ui &, int i) {
        tab_ = i;
        rebuild();
    });
    tabs_->set_focus_scope();
    tabs_->set_width(Dim::fill());
    sort_ = &head.add<PillButton>("", "more", PillButton::Style::Glass, [this](Ui &ui) { choose_sorting(ui); }, 56);
    sort_->name = "library.sort";
    empty_ = &col.add<Label>("");
    empty_->set_muted(true).set_max_lines(2);
    grid_ = &col.add<CollectionView>(Axis::Vertical, kColumns, wnk::Size{theme::kPosterW, theme::kPosterH + 56}, 28);
    grid_->set_width(Dim::fill()).set_height(Dim::fill());
    grid_->name = "library.grid";
    grid_->on_activate = [this](Ui &ui, size_t i) {
        if (i >= shown_.size()) return;
        const auto &sh = shown_[i].show;
        runtime::SearchResult item;
        item.name = sh.name;
        item.url = sh.url;
        item.api_name = sh.provider;
        item.type = sh.type;
        item.poster_url = sh.poster_url;
        item.year = sh.year;
        s_.router->open_details(ui, sh.provider, item);
    };
    add<TopBar>(*s_.router, app::Router::Tab::Library);
    sub_ = s_.library->changed.subscribe([this] { rebuild(); });
    rebuild();
}

void LibraryScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (!shown_once_) ui.focus_within(tabs_);
    shown_once_ = true;
}

void LibraryScreen::rebuild() {
    const auto &lib = s_.library->get();
    const auto titles = tab_titles(lib);
    for (size_t i = 0; i < titles.size() && i < tabs_->count(); i++) {
        if (auto *b = dynamic_cast<Button *>(&tabs_->tab(i))) b->set_text(titles[i]);
    }
    const library::WatchType type = library::watch_types()[static_cast<size_t>(tab_)];
    shown_ = lib.bookmarks(type);
    library::sort_bookmarks(shown_, sorting());
    sort_->set_text(library::sorting_label(sorting()));
    // Shows whose plugin is not loaded still open: Details reports what the provider answers.
    empty_->set_text(shown_.empty() ? std::string("Nothing in ") + library::watch_type_label(type) +
                                          " yet. Use the bookmark button on a show's page."
                                    : "");
    empty_->set_visible(shown_.empty());
    grid_->set_items(shown_.size(), [this](Ui &, size_t i) {
        const auto &sh = shown_[i].show;
        return std::make_unique<Card>(sh.name, sh.poster_url, sh.provider);
    });
}

library::ListSorting LibraryScreen::sorting() const {
    const long long v = s_.settings->get_int(settings::kLibrarySorting, static_cast<long long>(library::ListSorting::AlphabeticalA));
    for (library::ListSorting m : library::local_sortings()) {
        if (static_cast<long long>(m) == v) return m;
    }
    return library::ListSorting::AlphabeticalA;  // what the local list does with an unsupported one
}

void LibraryScreen::choose_sorting(Ui &ui) {
    auto d = std::make_unique<GlassDialog>("Sort");
    Dialog *dlg = d.get();
    View *current = nullptr;
    for (library::ListSorting m : library::local_sortings()) {
        auto &b = d->body().add<PillButton>(library::sorting_label(m), m == sorting() ? "check" : "", PillButton::Style::Glass,
                                            [this, dlg, m](Ui &u) {
                                                s_.settings->set(settings::kLibrarySorting, static_cast<int>(m));
                                                dlg->dismiss(u);
                                                rebuild();
                                            }, 60);
        b.name = "library.sort." + std::to_string(static_cast<int>(m));
        if (m == sorting()) current = &b;
    }
    d->add_button("Close");
    ui.show_dialog(std::move(d));
    if (current) ui.focus(current);
}

}  // namespace csweb::ui
