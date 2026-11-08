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
constexpr size_t kHistoryShown = 6;
constexpr const char *kCat = "csweb";
const wnk::Size kCell{theme::kPosterW, theme::kPosterH + 56};

bool blank(const std::string &s) { return s.find_first_not_of(" \t\r\n") == std::string::npos; }
}  // namespace

SearchScreen::SearchScreen(app::Services &s) : Screen("Search"), s_(s) {
    name = "search";
    auto &scroll = add<ScrollView>();
    scroll.margin = 160;
    auto &content = scroll.add<Column>(22);
    content.set_padding({theme::kSafeX, 150, theme::kSafeX, 60});
    content.set_width(Dim::fill());

    // The search bar across the screen (issue #26: it felt small in a side column).
    auto &bar = content.add<wnk::ui::Row>(18);
    field_ = &bar.add<TextField>("Search movies, shows, anime…");
    field_->name = "search.field";
    field_->set_max_length(120);
    field_->set_width(Dim::fixed(1180));
    field_->set_padding({28, 22, 28, 22});
    field_->set_on_change([this](Ui &ui, const std::string &text) { text_changed(ui, text); });
    field_->set_on_submit([this](Ui &ui, const std::string &q) { search(ui, q); });
    // Issue #22: closing LG's keyboard with Back keeps the text without searching, so the search
    // must also be reachable without the keyboard's Enter.
    auto &go = bar.add<PillButton>("Search", "search", PillButton::Style::Primary,
                                   [this](Ui &ui) { search(ui, field_->text()); }, 76);
    go.name = "search.go";
    status_ = &content.add<Label>("");
    status_->set_muted(true).set_max_lines(2);
    status_->set_font_size(theme::kLabel);
    status_->set_visible(false);

    chips_ = &content.add<TypeChips>(3, [this](Ui &ui, const std::set<std::string> &types) {
        settings::put_set(*s_.settings, settings::kSearchTypes, types);
        if (!query_.empty()) search(ui, query_);
    });
    chips_->set_width(Dim::fixed(780));  // three columns of chips, not spread across the screen
    refresh_chips();
    plugins_sub_ = s_.plugins_changed.subscribe([this] { refresh_chips(); });

    // Suggestions while typing (SearchSuggestionAdapter): select one to search it, Edit to put it in
    // the box, and a footer button to clear them.
    suggest_box_ = &content.add<Column>(12);
    suggest_box_->add<Label>("Suggestions", TextRole::Small).set_muted(true);
    suggestions_ = &suggest_box_->add<Column>(10);
    auto &clear_suggest = suggest_box_->add<PillButton>("Clear suggestions", "trash", PillButton::Style::Glass,
                                                        [this](Ui &ui) {
                                                            clear_suggestions();
                                                            ui.focus(field_);
                                                        }, 52);
    clear_suggest.name = "search.suggestions.clear";
    suggest_box_->set_visible(false);

    // Recent searches while the box is empty (SearchFragment: history only for a blank query).
    history_box_ = &content.add<Column>(12);
    auto &head = history_box_->add<wnk::ui::Row>(14);
    head.add<Label>("Recent searches", TextRole::Small).set_muted(true);
    auto &clear = head.add<PillButton>("Clear", "trash", PillButton::Style::Glass,
                                       [this](Ui &ui) { clear_history(ui); }, 48);
    clear.name = "search.history.clear";
    history_ = &history_box_->add<wnk::ui::Row>(12);
    show_history();

    results_ = &content.add<Column>(22);
    results_->set_width(Dim::fill());
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

bool SearchScreen::on_back(Ui &ui) {
    if (suggest_box_->visible()) {
        clear_suggestions();
        ui.focus(field_);
        return true;
    }
    return Screen::on_back(ui);
}

void SearchScreen::start_query(Ui &ui, const std::string &query) {
    field_->set_text(query);
    search(ui, query);
}

SearchScreen::~SearchScreen() {
    cancel();
    suggest_cancel_.cancel();
}

void SearchScreen::refresh_chips() {
    // The chips of the providers left after the language and media filters (SearchFragment.reloadRepos).
    const auto valid = settings::preferred_providers(s_.plugins->providers(), settings::read_filters(*s_.settings), false);
    chips_->set(valid, settings::get_set(*s_.settings, settings::kSearchTypes, settings::default_chip_types()));
}

void SearchScreen::cancel() {
    for (auto &kv : running_) s_.runtime->cancel(kv.first);
    running_.clear();
}

// ---- suggestions (issue #23) -------------------------------------------------------------------

void SearchScreen::text_changed(Ui &ui, const std::string &text) {
    // SearchFragment.onQueryTextChange: a blank box shows the history and no suggestions; otherwise
    // the suggestions are fetched (when on) after the debounce.
    if (blank(text)) {
        clear_suggestions();
        history_box_->set_visible(true);
        return;
    }
    history_box_->set_visible(false);
    if (!s_.settings->get_bool(settings::kSearchSuggestions, true)) return;
    suggest_text_ = text;
    suggest_due_ms_ = ui.now_ms() + search::kSuggestDebounceMs;
}

void SearchScreen::update(Ui &ui, float dt) {
    Screen::update(ui, dt);
    if (suggest_due_ms_ > 0 && ui.now_ms() >= suggest_due_ms_) {
        suggest_due_ms_ = 0;
        fetch_suggestions(suggest_text_);
    }
}

void SearchScreen::fetch_suggestions(const std::string &text) {
    suggest_cancel_.cancel();  // fetchSuggestions cancels the previous job
    suggest_cancel_ = wnk::async::CancellationSource();
    const std::string url = search::suggestions_url(text);
    if (url.empty()) {
        clear_suggestions();
        return;
    }
    if (const auto *cached = suggest_cache_.find(text)) {
        show_suggestions(*cached);
        return;
    }
    if (s_.http == nullptr) return;
    wnk::net::HttpRequest req;
    req.url = url;
    req.timeout_ms = 10000;
    req.max_response_bytes = 1u << 20;
    const bool sent = s_.http->send(req, suggest_cancel_.token(), life_.weak(), [this, text](wnk::net::HttpResponse res) {
        if (!res.ok()) {
            // getSuggestions logs and shows none. The URL carries the API key and the typed text.
            WNK_LOGD(kCat, "suggestions: no answer (status %ld)", res.status);
            return;
        }
        auto titles = search::parse_suggestions(res.body);
        suggest_cache_.put(text, titles);
        if (field_->text() != text || (!query_.empty() && query_ == text)) return;  // typed on, or searched
        show_suggestions(titles);
    });
    if (!sent) WNK_LOGD(kCat, "suggestions: request refused");
}

void SearchScreen::show_suggestions(const std::vector<std::string> &titles) {
    suggestions_->clear_children();
    for (size_t i = 0; i < titles.size(); ++i) {
        const std::string title = titles[i];
        auto &row = suggestions_->add<wnk::ui::Row>(12);
        auto &pick = row.add<PillButton>(title, "search", PillButton::Style::Glass, [this, title](Ui &ui) {
            // SEARCH_SUGGESTION_CLICK: the title becomes the query and is searched.
            field_->set_text(title);
            search(ui, title);
        }, 56);
        pick.name = "search.suggestion." + std::to_string(i);
        // One column of suggestions with Edit beside it: Down from the chips lands on a suggestion.
        pick.set_width(Dim::fixed(900));
        auto &fill = row.add<PillButton>("Edit", "", PillButton::Style::Glass, [this, title](Ui &ui) {
            // SEARCH_SUGGESTION_FILL: into the box without searching, to type on.
            field_->set_text(title);
            ui.focus(field_);
            suggest_text_ = title;
            suggest_due_ms_ = ui.now_ms() + search::kSuggestDebounceMs;
        }, 56);
        fill.accessible_label = "Put " + title + " in the search box";
        fill.name = "search.suggestion.fill." + std::to_string(i);
    }
    suggest_box_->set_visible(!titles.empty());
}

void SearchScreen::clear_suggestions() {
    suggest_due_ms_ = 0;
    suggest_cancel_.cancel();
    suggest_cancel_ = wnk::async::CancellationSource();
    suggestions_->clear_children();
    suggest_box_->set_visible(false);
}

// ---- history -----------------------------------------------------------------------------------

void SearchScreen::show_history() {
    history_->clear_children();
    auto it = s_.settings->get_string(kHistoryKey);
    const auto list = nlohmann::json::parse(it.empty() ? "[]" : it, nullptr, false);
    const bool any = list.is_array() && !list.empty();
    history_box_->set_visible(any && blank(field_->text()));
    if (!any) return;
    size_t n = 0;
    for (const auto &q : list) {
        if (!q.is_string() || n++ >= kHistoryShown) continue;
        const std::string query = q.get<std::string>();
        history_->add<PillButton>(query, "replay", PillButton::Style::Glass, [this, query](Ui &ui) {
            field_->set_text(query);
            search(ui, query);
        }, 56);
    }
}

void SearchScreen::clear_history(Ui &ui) {
    s_.settings->set(kHistoryKey, "[]");
    show_history();
    ui.focus(field_);  // the button just went away with the row
    ui.toast("Search history cleared.");
}

// ---- search ------------------------------------------------------------------------------------

void SearchScreen::update_status() {
    if (query_.empty()) {
        status_->set_text("");
        status_->set_visible(false);
        return;
    }
    status_->set_visible(true);
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
    clear_suggestions();  // onQueryTextSubmit
    cancel();
    failed_.clear();
    hint_->set_visible(false);
    for (auto &g : groups_) {
        results_->remove_child(g->title);
        results_->remove_child(g->rail);
    }
    groups_.clear();
    // SearchViewModel.search: one character or less clears the results instead of searching.
    if (search::code_points(q) <= 1) {
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
        status_->set_visible(true);
        status_->set_text("No sources are loaded. Install plugins in Settings › Extensions.");
        return;
    }
    if (active.empty()) {
        status_->set_visible(true);
        status_->set_text("No source matches the provider languages and media types chosen in Settings.");
        return;
    }
    update_status();
}

}  // namespace csweb::ui
