// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/home_screen.hpp"

#include "core/guard.hpp"
#include "settings/preferences.hpp"
#include "ui/dialogs/clear_watch_history.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/art.hpp"
#include "ui/widgets/pill_button.hpp"
#include "ui/widgets/top_bar.hpp"
#include "ui/widgets/type_chips.hpp"

#include "wnk/log.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <memory>
#include <set>

namespace csweb::ui {

using namespace wnk::ui;
using settings::kHomeProvider;
using settings::kNoneProvider;
using settings::kRandomProvider;

namespace {
constexpr const char *kCat = "csweb";
const wnk::Size kPosterCell{theme::kPosterW, theme::kPosterH + 56};
const wnk::Size kStillCell{theme::kStillW, theme::kStillH + 56};
}  // namespace

HomeScreen::HomeScreen(app::Services &s) : Screen("Home"), s_(s) {
    name = "home";
    backdrop_ = &add<Backdrop>();
    backdrop_->set_width(Dim::fill()).set_height(Dim::fixed(840));
    backdrop_->set_align(Align::Stretch, Align::Start);

    auto &scroll = add<ScrollView>();
    scroll.margin = 160;
    content_ = &scroll.add<Column>(28);
    content_->set_padding({theme::kSafeX, 300, theme::kSafeX, 80});

    hero_ = &content_->add<Column>(18);
    hero_->set_width(Dim::fixed(880));
    hero_kicker_ = &hero_->add<Label>("", TextRole::Small);
    hero_kicker_->set_bold(true).set_color(theme::kAccent).set_font_size(theme::kCaption);
    hero_title_ = &hero_->add<Label>("", TextRole::Title);
    hero_title_->set_font_size(theme::kHero).set_bold(true).set_max_lines(2);
    hero_meta_ = &hero_->add<Label>("");
    hero_meta_->set_color(theme::kTextSecondary).set_font_size(theme::kBody);
    hero_plot_ = &hero_->add<Label>("");
    hero_plot_->set_color(theme::kTextSecondary).set_font_size(theme::kLabel).set_max_lines(3);
    auto &buttons = hero_->add<wnk::ui::Row>(18);
    buttons.set_padding({0, 14, 0, 0});
    hero_play_ = &buttons.add<PillButton>("Details", "play", PillButton::Style::Primary, [this](Ui &ui) {
        if (!hero_item_.url.empty()) s_.router->open_details(ui, provider_, hero_item_);
    }, 72);
    hero_play_->name = "home.hero.open";
    hero_info_ = &buttons.add<PillButton>("", "refresh", PillButton::Style::Glass, [this](Ui &) { pick_hero(); }, 72);
    hero_info_->accessible_label = "Another pick";
    hero_info_->set_frosted(true);
    hero_->set_visible(false);

    state_ = &content_->add<Column>(24);
    state_text_ = &state_->add<Label>("Loading plugins…", TextRole::Title);
    state_text_->set_max_lines(3);
    state_button_ = &state_->add<PillButton>("Open extensions", "extensions", PillButton::Style::Primary, [this](Ui &ui) {
        if (state_action_ == StateAction::Extensions) s_.router->open_extensions(ui);
        else open_provider_picker(ui);
    });
    state_button_->name = "home.state.action";
    state_button_->set_visible(false);

    resume_title_ = &content_->add<wnk::ui::Row>(20);
    resume_title_->add<Label>("Continue watching").set_bold(true).set_font_size(theme::kHeadline);
    auto &clear_resume = resume_title_->add<PillButton>("Clear history", "trash", PillButton::Style::Glass,
                                                        [this](Ui &ui) {
        // The row goes away with its button: keep focus near it, not on the profile button.
        confirm_clear_watch_history(ui, s_, [this](Ui &u) {
            u.focus(hero_->visible() ? static_cast<View *>(hero_play_) : static_cast<View *>(top_->current_tab()));
        });
    }, 48);
    clear_resume.name = "home.resume.clear";
    resume_rail_ = &content_->add<CollectionView>(Axis::Horizontal, 1, kStillCell, 28);
    resume_rail_->set_width(Dim::fill());
    resume_rail_->set_height(Dim::fixed(kStillCell.h + 40));
    resume_rail_->name = "home.resume";
    resume_rail_->on_activate = [this](Ui &ui, size_t i) {
        if (i >= resume_.size()) return;
        const auto &r = resume_[i];
        runtime::SearchResult item;
        item.name = r.show.name;
        item.url = r.show.url;
        item.api_name = r.show.provider;
        item.type = r.show.type;
        item.poster_url = r.show.poster_url;
        item.year = r.show.year;
        s_.router->open_details(ui, r.show.provider, item, true);
    };
    library_sub_ = s_.library->changed.subscribe([this] { rebuild_resume(); });
    profile_sub_ = s_.profile_changed.subscribe([this] {
        Ui *u = this->ui();
        if (u == nullptr) return;
        refresh(*u);
        if (s_.router->nav().top() == this) u->focus(top_->current_tab());
    });
    rebuild_resume();

    rows_box_ = &content_->add<Column>(26);

    top_ = &add<TopBar>(*s_.router, app::Router::Tab::Home);
    provider_button_ = &top_->right().add<PillButton>("Source", "chevron-down", PillButton::Style::Glass,
                                                     [this](Ui &ui) { open_provider_picker(ui); }, 60);
    provider_button_->name = "home.provider";
    provider_button_->set_frosted(true);
    clock_ = &top_->right().add<Label>("");
    clock_->set_bold(true);

    plugins_sub_ = s_.plugins_changed.subscribe([this] {
        if (Ui *u = ui()) refresh(*u);
    });
}

HomeScreen::~HomeScreen() { cancel_requests(); }

void HomeScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (!shown_) ui.focus(top_->current_tab());  // not the profile button before it
    shown_ = true;
    refresh(ui);
}

void HomeScreen::rebuild_resume() {
    resume_ = s_.library->get().resume_list();
    resume_title_->set_visible(!resume_.empty());
    resume_rail_->set_visible(!resume_.empty());
    resume_rail_->set_items(resume_.size(), [this](Ui &, size_t i) {
        const auto &r = resume_[i];
        auto card = std::make_unique<Card>(r.show.name, r.show.poster_url, r.episode.name);
        card->image_aspect = 9.0f / 16.0f;
        return card;
    });
}

void HomeScreen::cancel_requests() {
    for (auto id : requests_) s_.runtime->cancel(id);
    requests_.clear();
    pending_.clear();
}

std::string HomeScreen::chosen_provider() const {
    // HomeViewModel.loadAndCancel: the stored provider if it is loaded, else nothing (CloudStream does
    // not pick one by itself; the viewer chooses).
    const std::string want = s_.settings->get_string(kHomeProvider);
    for (const auto &p : s_.plugins->providers()) {
        if (p.name == want) return p.name;
    }
    return {};
}

std::string HomeScreen::random_provider() const {
    // HomeViewModel: a random provider among those the settings allow on Home.
    std::vector<std::string> with_home;
    for (const auto &p : settings::preferred_providers(s_.plugins->providers(), settings::read_filters(*s_.settings), true))
        with_home.push_back(p.name);
    if (with_home.empty()) return {};
    return with_home[std::uniform_int_distribution<size_t>(0, with_home.size() - 1)(rng_)];
}

void HomeScreen::set_state_text(const std::string &text, StateAction action) {
    state_->set_visible(!text.empty());
    state_text_->set_text(text);
    state_action_ = action;
    state_button_->set_visible(action != StateAction::None);
    state_button_->set_text(action == StateAction::Extensions ? "Open extensions" : "Choose a source");
    state_button_->set_icon(action == StateAction::Extensions ? "extensions" : "chevron-down");
}

void HomeScreen::refresh(Ui &ui) {
    if (s_.runtime->state() == runtime::RuntimeClient::State::Failed) {
        set_state_text("The plugin runtime could not start: " + s_.runtime->last_error(), StateAction::None);
        provider_button_->set_text("Source");
        return;
    }
    if (s_.plugins->installed().empty() && !s_.plugins->loading()) {
        set_state_text("No extensions installed yet. Add a repository, then install the plugins you want.",
                       StateAction::Extensions);
        provider_button_->set_text("Source");
        return;
    }
    const std::string want = chosen_provider();
    if (want.empty() && s_.plugins->loading()) {
        set_state_text("Loading plugins…", StateAction::None);  // the stored provider may still load
        return;
    }
    if (want == provider_ && (!rows_.empty() || !requests_.empty() || !pending_.empty())) return;
    load(ui, want);
}

void HomeScreen::load(Ui &ui, const std::string &provider) {
    cancel_requests();
    rows_.clear();
    rows_box_->clear_children();
    hero_seen_.clear();
    hero_set_ = false;
    hero_->set_visible(false);
    backdrop_->set_url({});
    provider_ = provider;
    provider_button_->set_text(provider.empty() ? "Source: none" : provider);
    if (provider.empty()) {
        set_state_text("Choose the source shown here, or use Search.", StateAction::ChooseSource);
        return;
    }
    const runtime::Provider *p = s_.plugins->provider(provider);
    if (!p || !p->has_main_page || p->main_page.empty()) {
        set_state_text(provider + " has no home page. Choose another source, or use Search.", StateAction::ChooseSource);
        return;
    }
    set_state_text("Loading " + provider + "…", StateAction::None);
    WNK_LOGI(kCat, "home: %s, %zu sections%s", provider.c_str(), p->main_page.size(), p->sequential_main_page ? " (sequential)" : "");
    const double now = ui.now_ms();
    requested_ms_ = now;
    first_row_logged_ = false;
    for (size_t i = 0; i < p->main_page.size(); i++) {
        const double at = p->sequential_main_page ? now + double(i) * double(p->sequential_delay_ms) : now;
        pending_.push_back({static_cast<int>(i), at});
    }
    // Sequential providers: one section at a time (the next is scheduled when the previous ends).
}

void HomeScreen::update(Ui &ui, float dt) {
    Screen::update(ui, dt);
    const runtime::Provider *p = provider_.empty() ? nullptr : s_.plugins->provider(provider_);
    const bool sequential = p && p->sequential_main_page;
    while (!pending_.empty() && pending_.front().not_before_ms <= ui.now_ms() && (!sequential || requests_.empty())) {
        const int index = pending_.front().index;
        pending_.pop_front();
        request_section(ui, index);
    }
    if (ui.now_ms() - last_clock_ms_ > 10000) {
        last_clock_ms_ = ui.now_ms();
        const std::time_t t = std::time(nullptr);
        char buf[8];
        std::strftime(buf, sizeof buf, "%H:%M", std::localtime(&t));
        clock_->set_text(buf);
    }
}

HomeScreen::Row &HomeScreen::row_for(const std::string &name, bool horizontal) {
    for (auto &r : rows_) if (r->name == name) return *r;
    auto row = std::make_unique<Row>();
    row->name = name;
    row->horizontal = horizontal;
    row->title = &rows_box_->add<Label>(name.empty() ? provider_ : name);
    row->title->set_bold(true).set_font_size(theme::kHeadline);
    const wnk::Size cell = horizontal ? kStillCell : kPosterCell;
    row->rail = &rows_box_->add<CollectionView>(Axis::Horizontal, 1, cell, 28);
    row->rail->set_width(Dim::fill());
    row->rail->set_height(Dim::fixed(cell.h + 40));
    Row *raw = row.get();
    row->rail->on_activate = [this, raw](Ui &ui, size_t i) {
        if (i < raw->items.size()) s_.router->open_details(ui, provider_, raw->items[i]);
    };
    rows_.push_back(std::move(row));
    return *rows_.back();
}

void HomeScreen::rebuild_row(Row &row) {
    Row *raw = &row;
    row.rail->set_items(row.items.size(), [raw](Ui &, size_t i) {
        const auto &it = raw->items[i];
        auto card = std::make_unique<Card>(it.name, it.poster_url, it.year > 0 ? std::to_string(it.year) : std::string());
        card->image_aspect = raw->horizontal ? 9.0f / 16.0f : 1.5f;
        return card;
    });
}

void HomeScreen::request_section(Ui &, int index) {
    const std::string provider = provider_;
    // One request answers one MainPageData; its HomePageResponse may hold several HomePageLists, each
    // a row of its own (a "section" event starts each).
    struct List {
        std::string name;
        bool horizontal = false;
        std::vector<runtime::SearchResult> items;
    };
    auto acc = std::make_shared<std::vector<List>>();
    const runtime::Provider *p = s_.plugins->provider(provider);
    nlohmann::json req = {{"op", "mainpage"}, {"provider", provider}, {"section", index}, {"page", 1}, {"limit", 60}};
    if (p && p->main_page_timeout_ms > 0) req["timeout"] = (p->main_page_timeout_ms + 999) / 1000;
    auto id = std::make_shared<runtime::RuntimeClient::RequestId>(0);
    *id = s_.runtime->request(
        req,
        [acc](const nlohmann::json &e) {
            const std::string ev = e.value("event", "");
            if (ev == "section") {
                List l;
                l.name = e.value("name", "");
                l.horizontal = e.contains("horizontal") && e["horizontal"].is_boolean() && e["horizontal"].get<bool>();
                acc->push_back(std::move(l));
            } else if (ev == "result") {
                if (acc->empty()) acc->emplace_back();
                acc->back().items.push_back(runtime::parse_search_result(e));
            }
        },
        core::guarded(life_.weak(), [this, acc, provider, id](bool ok, const std::string &error) {
            requests_.erase(std::remove(requests_.begin(), requests_.end(), *id), requests_.end());
            if (provider != provider_) return;
            size_t items = 0;
            for (const List &l : *acc) items += l.items.size();
            if (!ok && items == 0) {
                WNK_LOGW(kCat, "home: section failed: %s", error.c_str());
            }
            if (items > 0 && !first_row_logged_) {
                // Start-up measurements (docs/performance.md): when Home has something to show.
                first_row_logged_ = true;
                if (Ui *u = this->ui()) {
                    WNK_LOGI(kCat, "home: first row after %.0f ms", u->now_ms() - requested_ms_);
                }
            }
            for (List &l : *acc) {
                if (l.items.empty()) continue;
                Row &row = row_for(l.name, l.horizontal);
                for (auto &it : l.items) row.items.push_back(std::move(it));
                rebuild_row(row);
            }
            if (items > 0 && !hero_set_) pick_hero();
            if (requests_.empty() && pending_.empty()) {
                set_state_text(rows_.empty() ? provider_ + " returned nothing. Try again later or choose another source." : "",
                               rows_.empty() ? StateAction::ChooseSource : StateAction::None);
            } else if (!rows_.empty()) {
                set_state_text("", StateAction::None);
            }
        }));
    if (*id != 0) requests_.push_back(*id);
}

void HomeScreen::pick_hero() {
    // CloudStream's preview: shuffle every item of every row, distinct by url, and load the details.
    std::vector<runtime::SearchResult> all;
    std::set<std::string> seen(hero_seen_.begin(), hero_seen_.end());
    for (const auto &r : rows_) {
        for (const auto &it : r->items) {
            if (!it.url.empty() && seen.insert(it.url).second) all.push_back(it);
        }
    }
    if (all.empty()) return;
    std::shuffle(all.begin(), all.end(), rng_);
    const runtime::SearchResult item = all.front();
    hero_seen_.push_back(item.url);
    hero_set_ = true;
    const std::string provider = provider_;
    auto details = std::make_shared<runtime::Details>();
    s_.runtime->request({{"op", "details"}, {"provider", provider}, {"url", item.url}, {"limit", 1}},
                        [details](const nlohmann::json &e) {
                            if (e.value("event", "") == "details") *details = runtime::parse_details(e);
                        },
                        core::guarded(life_.weak(), [this, item, details, provider](bool ok, const std::string &) {
                            if (provider != provider_) return;
                            if (!ok || details->name.empty()) {
                                hero_set_ = false;
                                if (hero_seen_.size() < 5) pick_hero();  // try another item, a few times
                                return;
                            }
                            hero_item_ = item;
                            show_hero(*details);
                        }));
}

void HomeScreen::show_hero(const runtime::Details &d) {
    hero_->set_visible(true);
    hero_kicker_->set_text("FEATURED · " + provider_);
    hero_title_->set_text(d.name);
    std::string meta;
    auto add = [&meta](const std::string &s) {
        if (s.empty()) return;
        if (!meta.empty()) meta += "  ·  ";
        meta += s;
    };
    add(d.type);
    if (d.year > 0) add(std::to_string(d.year));
    if (d.duration > 0) add(std::to_string(d.duration) + " min");
    if (!d.score.empty() && d.score != "null") add("★ " + d.score);
    if (!d.content_rating.empty() && d.content_rating != "null") add(d.content_rating);
    hero_meta_->set_text(meta);
    hero_plot_->set_text(d.plot == "null" ? std::string() : d.plot);
    backdrop_->set_url(!d.background_url.empty() ? d.background_url : d.poster_url);
}

void HomeScreen::open_provider_picker(Ui &ui) {
    auto dialog = std::make_unique<Dialog>("Choose a source", "The home page shows one source at a time.");
    Dialog *dlg = dialog.get();
    // HomeFragment.selectHomepage: the providers left by the language and media settings, the type
    // chips (home_pref_homepage) narrowing the list.
    const auto valid = settings::preferred_providers(s_.plugins->providers(), settings::read_filters(*s_.settings), true);
    auto *list = static_cast<Column *>(nullptr);
    auto fill = std::make_shared<std::function<void(const std::set<std::string> &)>>();
    auto &chips = dlg->body().add<TypeChips>(5, [this, fill](Ui &, const std::set<std::string> &types) {
        settings::put_set(*s_.settings, settings::kHomeTypes, types);
        (*fill)(types);
    });
    chips.set_width(Dim::fixed(1100));
    auto &scroll = dlg->body().add<ScrollView>();
    scroll.set_height(Dim::fixed(460));
    list = &scroll.add<Column>(8);
    // As HomeViewModel.loadAndCancel(fromUI = true): None and a provider are stored as chosen; Random
    // picks one now and stores that provider.
    auto choose = [this, dlg](const std::string &value) {
        return [this, dlg, value](Ui &ui) {
            const std::string name = value == kRandomProvider ? random_provider() : value;
            s_.settings->set(kHomeProvider, name.empty() ? std::string(kNoneProvider) : name);
            dlg->dismiss(ui);
            load(ui, chosen_provider());
        };
    };
    *fill = [this, list, valid, choose](const std::set<std::string> &types) {
        list->clear_children();
        list->add<PillButton>("None", "", PillButton::Style::Glass, choose(kNoneProvider), 60);
        list->add<PillButton>("Random", "refresh", PillButton::Style::Glass, choose(kRandomProvider), 60);
        // None and Random, then the providers by lower-case name (pinned providers are not built).
        for (const auto &p : settings::home_providers(valid, types)) {
            auto &b = list->add<PillButton>(p.name, p.name == provider_ ? "check" : "", PillButton::Style::Glass, choose(p.name), 60);
            b.name = "provider." + p.name;
        }
    };
    const auto types = settings::get_set(*s_.settings, settings::kHomeTypes, settings::default_chip_types());
    chips.set(valid, types);
    (*fill)(types);
    dlg->add_button("Close");
    ui.show_dialog(std::move(dialog));
}

}  // namespace csweb::ui
