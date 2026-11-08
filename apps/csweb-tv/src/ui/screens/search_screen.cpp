// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/search_screen.hpp"

#include "core/guard.hpp"
#include "settings/preferences.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/pill_button.hpp"
#include "ui/widgets/top_bar.hpp"
#include "ui/widgets/type_chips.hpp"
#include "wnk/log.hpp"

#include <algorithm>

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr const char *kHistoryKey = settings::kSearchHistory;
constexpr size_t kHistoryMax = 10;
constexpr const char *kCat = "csweb";
const wnk::Size kCell{theme::kPosterW, theme::kPosterH + 56};
}  // namespace

SearchScreen::SearchScreen(app::Services &s) : Screen("Search"), s_(s) {
    name = "search";
    auto &body = add<wnk::ui::Row>(56);
    body.set_padding({theme::kSafeX, 170, 0, 40});

    auto &left = body.add<Column>(22);
    left.set_width(Dim::fixed(560));
    left.add<Label>("Search all sources", TextRole::Small).set_muted(true);
    field_ = &left.add<TextField>("Title, actor…");
    field_->name = "search.field";
    field_->set_max_length(120);
    field_->set_on_submit([this](Ui &ui, const std::string &q) { search(ui, q); });
    // Issue #22: closing LG's keyboard with Back keeps the text without searching, so the search
    // must also be reachable without the keyboard's Enter.
    auto &actions = left.add<wnk::ui::Row>(14);
    auto &go = actions.add<PillButton>("Search", "search", PillButton::Style::Primary,
                                       [this](Ui &ui) { search(ui, field_->text()); }, 60);
    go.name = "search.go";
    status_ = &left.add<Label>("");
    status_->set_muted(true).set_max_lines(3);
    chips_ = &left.add<TypeChips>(3, [this](Ui &ui, const std::set<std::string> &types) {
        settings::put_set(*s_.settings, settings::kSearchTypes, types);
        if (!query_.empty()) search(ui, query_);
    });
    chips_->set_width(Dim::fill());
    refresh_chips();
    plugins_sub_ = s_.plugins_changed.subscribe([this] { refresh_chips(); });
    history_head_ = &left.add<wnk::ui::Row>(14);
    history_head_->set_padding({0, 20, 0, 0});
    history_head_->add<Label>("Recent", TextRole::Small).set_muted(true);
    auto &clear = history_head_->add<PillButton>("Clear", "trash", PillButton::Style::Glass,
                                                 [this](Ui &ui) { clear_history(ui); }, 48);
    clear.name = "search.history.clear";
    history_ = &left.add<wnk::ui::Row>(10);
    show_history();

    auto &scroll = body.add<ScrollView>();
    scroll.set_width(Dim::fill());
    scroll.margin = 120;
    results_ = &scroll.add<Column>(22);
    results_->set_padding({0, 0, theme::kSafeX, 60});
    hint_ = &results_->add<Label>("Results from every installed source appear here, grouped by source.");
    hint_->set_muted(true).set_max_lines(2);

    add<TopBar>(*s_.router, app::Router::Tab::Search);
}

void SearchScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (shown_) {  // the filters or the history may have changed in Settings
        refresh_chips();
        show_history();
    }
    if (!shown_) ui.focus(field_);  // opened from the nav bar: straight to typing
    shown_ = true;
}

void SearchScreen::start_query(Ui &ui, const std::string &query) {
    field_->set_text(query);
    search(ui, query);
}

SearchScreen::~SearchScreen() { cancel(); }

void SearchScreen::refresh_chips() {
    // The chips of the providers left after the language and media filters (SearchFragment.reloadRepos).
    const auto valid = settings::preferred_providers(s_.plugins->providers(), settings::read_filters(*s_.settings), false);
    chips_->set(valid, settings::get_set(*s_.settings, settings::kSearchTypes, settings::default_chip_types()));
}

void SearchScreen::cancel() {
    for (auto &kv : running_) s_.runtime->cancel(kv.first);
    running_.clear();
}

void SearchScreen::show_history() {
    history_->clear_children();
    auto it = s_.settings->get_string(kHistoryKey);
    const auto list = nlohmann::json::parse(it.empty() ? "[]" : it, nullptr, false);
    history_head_->set_visible(list.is_array() && !list.empty());
    if (!list.is_array()) return;
    size_t n = 0;
    for (const auto &q : list) {
        if (!q.is_string() || n++ >= 4) continue;  // the row shows the four most recent
        const std::string query = q.get<std::string>();
        history_->add<PillButton>(query, "", PillButton::Style::Glass, [this, query](Ui &ui) {
            field_->set_text(query);
            search(ui, query);
        }, 52);
    }
}

void SearchScreen::clear_history(Ui &ui) {
    s_.settings->set(kHistoryKey, "[]");
    show_history();
    ui.focus(field_);  // the button just went away with the row
    ui.toast("Search history cleared.");
}

void SearchScreen::update_status() {
    if (query_.empty()) {
        status_->set_text("");
        return;
    }
    size_t results = 0;
    for (const auto &g : groups_) results += g->items.size();
    std::string text;
    if (!running_.empty()) {
        std::string names;
        for (auto &kv : running_) names += (names.empty() ? "" : ", ") + kv.second;
        text = "Searching " + std::to_string(running_.size()) + " of " + std::to_string(asked_) + " sources: " + names;
    } else if (results == 0) {
        text = "Nothing found for “" + query_ + "”.";
    } else {
        text = std::to_string(results) + " results from " + std::to_string(groups_.size()) + " of " +
               std::to_string(asked_) + (asked_ == 1 ? " source." : " sources.");
    }
    if (!failed_.empty()) {
        // One reason for all (the runtime is down) reads better than the same reason per source.
        bool same = true;
        for (auto &f : failed_) same = same && f.second == failed_.front().second;
        std::string names;
        for (auto &f : failed_) names += (names.empty() ? "" : ", ") + f.first;
        text += "  ·  No answer from " + names;
        if (same && !failed_.front().second.empty()) text += " (" + failed_.front().second + ")";
    }
    // Issue #22: a search that only asked some sources must say so, or it looks like it found nothing.
    if (skipped_ > 0) {
        text += "  ·  " + std::to_string(skipped_) + (skipped_ == 1 ? " source" : " sources") +
                " skipped by the type and language filters";
    }
    status_->set_text(text);
}

void SearchScreen::search(Ui &, const std::string &raw) {
    std::string q = raw;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    if (q.empty()) return;
    cancel();
    failed_.clear();
    hint_->set_visible(false);
    for (auto &g : groups_) {
        results_->remove_child(g->title);
        results_->remove_child(g->rail);
    }
    groups_.clear();
    // SearchViewModel.search: one character or less clears the results instead of searching.
    size_t code_points = 0;
    for (unsigned char c : q) code_points += (c & 0xC0) != 0x80;
    if (code_points <= 1) {
        query_.clear();
        update_status();
        return;
    }
    query_ = q;

    // History: most recent first, no duplicates (SearchHistoryAdaptor).
    auto hist = nlohmann::json::parse(s_.settings->get_string(kHistoryKey, "[]"), nullptr, false);
    nlohmann::json next = nlohmann::json::array({q});
    if (hist.is_array()) {
        for (const auto &h : hist) {
            if (h.is_string() && h.get<std::string>() != q && next.size() < kHistoryMax) next.push_back(h);
        }
    }
    s_.settings->set(kHistoryKey, next.dump());
    show_history();

    const auto active = settings::search_providers(s_.plugins->providers(), settings::read_filters(*s_.settings),
                                                   chips_->selected());
    asked_ = active.size();
    skipped_ = s_.plugins->providers().size() - std::min(active.size(), s_.plugins->providers().size());
    WNK_LOGI(kCat, "search: %zu of %zu sources", active.size(), s_.plugins->providers().size());  // not the query
    for (const std::string &provider : active) {
        auto items = std::make_shared<std::vector<runtime::SearchResult>>();
        auto empty = std::make_shared<bool>(false);  // answered with no results (the runtime's "empty")
        auto id = std::make_shared<runtime::RuntimeClient::RequestId>(0);
        *id = s_.runtime->request(
            {{"op", "search"}, {"provider", provider}, {"query", q}, {"limit", 60}},
            [items, empty](const nlohmann::json &e) {
                const std::string ev = e.value("event", "");
                if (ev == "result") items->push_back(runtime::parse_search_result(e));
                else if (ev == "empty") *empty = true;
            },
            core::guarded(life_.weak(), [this, provider, items, empty, id, q](bool ok, const std::string &error) {
                running_.erase(*id);
                if (q != query_) return;
                if (!ok && items->empty() && !*empty) failed_.emplace_back(provider, error);
                if (!items->empty()) {
                    auto g = std::make_unique<Group>();
                    g->provider = provider;
                    g->items = std::move(*items);
                    g->title = &results_->add<Label>(provider + "   " + std::to_string(g->items.size()) + " results");
                    g->title->set_bold(true).set_font_size(theme::kHeadline);
                    g->rail = &results_->add<CollectionView>(Axis::Horizontal, 1, kCell, 24);
                    g->rail->set_width(Dim::fill()).set_height(Dim::fixed(kCell.h + 40));
                    Group *raw = g.get();
                    g->rail->set_items(raw->items.size(), [raw](Ui &, size_t i) {
                        const auto &it = raw->items[i];
                        auto card = std::make_unique<Card>(it.name, it.poster_url, it.year > 0 ? std::to_string(it.year) : std::string());
                        card->image_aspect = 1.5f;
                        return card;
                    });
                    g->rail->on_activate = [this, raw](Ui &ui, size_t i) {
                        if (i < raw->items.size()) s_.router->open_details(ui, raw->provider, raw->items[i]);
                    };
                    groups_.push_back(std::move(g));
                }
                update_status();
                if (running_.empty()) {
                    size_t results = 0;
                    for (const auto &g : groups_) results += g->items.size();
                    WNK_LOGI(kCat, "search: done, %zu results from %zu of %zu sources, %zu failed", results,
                             groups_.size(), asked_, failed_.size());
                }
            }));
        if (*id != 0) running_[*id] = provider;
    }
    if (s_.plugins->providers().empty()) {
        status_->set_text("No sources are loaded. Install plugins in Settings › Extensions.");
        return;
    }
    if (active.empty()) {
        status_->set_text("No source matches the provider languages and media types chosen in Settings.");
        return;
    }
    update_status();
}

}  // namespace csweb::ui
