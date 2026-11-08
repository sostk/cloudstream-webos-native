// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/details_screen.hpp"

#include "core/guard.hpp"
#include "settings/preferences.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/art.hpp"
#include "ui/widgets/episode_row.hpp"
#include "ui/widgets/pill_button.hpp"

#include "wnk/ui/forms.hpp"

#include "wnk/log.hpp"


namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr const char *kCat = "csweb";

bool present(const std::string &s) { return !s.empty() && s != "null"; }
}  // namespace

// ---- screen -------------------------------------------------------------------------------------------

namespace {
constexpr float kActionH = 84;  // Play, Resume and the library button (issue #26: 72 read small on a TV)
}  // namespace

DetailsScreen::DetailsScreen(app::Services &s, std::string provider, runtime::SearchResult item, bool resume)
    : Screen(item.name), s_(s), provider_(std::move(provider)), item_(std::move(item)), resume_on_load_(resume) {
    name = "details";
    backdrop_ = &add<Backdrop>(item_.poster_url);
    backdrop_->set_width(Dim::fill()).set_height(Dim::fixed(1080));
    backdrop_->bottom_extent = 0.6f;

    auto &scroll = add<ScrollView>();
    scroll.margin = 200;
    content_ = &scroll.add<Column>(22);
    content_->set_padding({theme::kSafeX, 200, theme::kSafeX, 80});

    auto &head = content_->add<Column>(16);
    head.set_width(Dim::fixed(1000));
    auto &kicker = head.add<Label>(provider_, TextRole::Small);
    kicker.set_bold(true).set_color(theme::kAccent).set_font_size(theme::kCaption);
    title_ = &head.add<Label>(item_.name, TextRole::Title);
    title_->set_font_size(theme::kDisplay).set_bold(true).set_max_lines(2);
    meta_ = &head.add<Label>("");
    meta_->set_color(theme::kTextSecondary);
    tags_ = &head.add<Label>("", TextRole::Small);
    tags_->set_color(theme::kAccent).set_max_lines(1);
    plot_ = &head.add<Label>("");
    plot_->set_color(theme::kTextSecondary).set_font_size(theme::kLabel).set_max_lines(4);
    // Notes and errors only; while loading, the button row says so (issue #26: a small "Loading…"
    // above a lone bookmark button looked out of place).
    status_ = &head.add<Label>("");
    status_->set_color(theme::kTextSecondary).set_font_size(theme::kBody).set_max_lines(3);
    status_->set_visible(false);

    auto &buttons = content_->add<wnk::ui::Row>(20);
    buttons.set_padding({0, 10, 0, 10});
    loading_ = &buttons.add<PillButton>("Loading…", "refresh", PillButton::Style::Glass, [](Ui &) {}, kActionH);
    loading_->set_frosted(true);
    loading_->set_focusable(false);
    loading_->name = "details.loading";
    play_ = &buttons.add<PillButton>("Play", "play", PillButton::Style::Primary, [this](Ui &ui) {
        const auto it = groups_.find(group_);
        play(ui, it != groups_.end() && !it->second.empty() ? &it->second.front() : nullptr);
    }, kActionH);
    play_->name = "details.play";
    play_->set_visible(false);
    resume_ = &buttons.add<PillButton>("Resume", "play", PillButton::Style::Primary, [this](Ui &ui) { play_resume(ui); }, kActionH);
    resume_->name = "details.resume";
    resume_->set_visible(false);
    bookmark_ = &buttons.add<PillButton>("Add to library", "bookmark", PillButton::Style::Glass, [this](Ui &ui) { choose_list(ui); }, kActionH);
    bookmark_->set_frosted(true);
    bookmark_->name = "details.bookmark";
    library_sub_ = s_.library->changed.subscribe([this] { refresh_library(); });

    group_row_ = &content_->add<wnk::ui::Row>(14);
    group_row_->set_focus_scope();
    range_row_ = &content_->add<wnk::ui::Row>(14);
    range_row_->set_focus_scope();
    episodes_ = &content_->add<Column>(10);
    episodes_->set_width(Dim::fixed(1300));
    episodes_->set_focus_scope();

    cast_title_ = &content_->add<Label>("Cast");
    cast_title_->set_bold(true).set_font_size(theme::kHeadline).set_visible(false);
    cast_ = &content_->add<CollectionView>(Axis::Horizontal, 1, wnk::Size{180, 180 + 84}, 24);
    cast_->set_width(Dim::fill());
    cast_->set_height(Dim::fixed(180 + 124));
    cast_->set_visible(false);
    cast_->name = "details.cast";

    more_title_ = &content_->add<Label>("More like this");
    more_title_->set_bold(true).set_font_size(theme::kHeadline).set_visible(false);
    more_ = &content_->add<CollectionView>(Axis::Horizontal, 1, wnk::Size{theme::kPosterW, theme::kPosterH + 56}, 28);
    more_->set_width(Dim::fill());
    more_->set_height(Dim::fixed(theme::kPosterH + 96));
    more_->set_visible(false);
    more_->on_activate = [this](Ui &ui, size_t i) {
        if (i < details_.recommendations.size()) s_.router->open_details(ui, provider_, details_.recommendations[i]);
    };
}

DetailsScreen::~DetailsScreen() {
    if (request_ != 0) s_.runtime->cancel(request_);
}

void DetailsScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (!loaded_ && request_ == 0) load(ui);
}

void DetailsScreen::load(Ui &) {
    // Opened before the plugins have loaded (Continue watching, the library, a link at start): wait
    // for the provider instead of failing with "no loaded provider".
    if (s_.plugins->provider(provider_) == nullptr && s_.plugins->loading()) {
        loading_->set_text("Loading " + provider_ + "…");
        if (!waiting_plugins_) {
            waiting_plugins_ = true;
            plugins_sub_ = s_.plugins_changed.subscribe([this] {
                if (s_.plugins->provider(provider_) == nullptr && s_.plugins->loading()) return;
                if (Ui *u = this->ui(); u != nullptr && !loaded_ && request_ == 0) load(*u);
            });
        }
        return;
    }
    if (s_.plugins->provider(provider_) == nullptr) {
        loading_->set_visible(false);
        status_->set_text(provider_ + " is not loaded. Install or enable its plugin in Settings › Extensions.");
        status_->set_visible(true);
        return;
    }
    auto details = std::make_shared<runtime::Details>();
    auto episodes = std::make_shared<std::vector<runtime::Episode>>();
    auto got = std::make_shared<bool>(false);
    request_ = s_.runtime->request(
        {{"op", "details"}, {"provider", provider_}, {"url", item_.url}, {"limit", 100000}},
        [details, episodes, got](const nlohmann::json &e) {
            const std::string ev = e.value("event", "");
            if (ev == "details") {
                *details = runtime::parse_details(e);
                *got = true;
            } else if (ev == "episode") {
                episodes->push_back(runtime::parse_episode(e));
            }
        },
        core::guarded(life_.weak(), [this, details, episodes, got](bool ok, const std::string &error) {
            request_ = 0;
            if (!*got) {
                WNK_LOGW(kCat, "details failed: %s", error.c_str());
                loading_->set_visible(false);
                status_->set_text(error.empty() ? "This item could not be loaded." : "Could not load: " + error);
                status_->set_visible(true);
                return;
            }
            if (!ok) WNK_LOGW(kCat, "details ended early: %s", error.c_str());
            loaded_ = true;
            details_ = std::move(*details);
            details_.episodes = std::move(*episodes);
            if (Ui *u = this->ui()) show(*u);
        }));
}

std::string DetailsScreen::group_label(const runtime::EpisodeGroup &g) const {
    std::string label;
    if (groups_.size() > 1 || g.season > 0) {
        for (const auto &s : details_.seasons) {
            if (s.season == g.season && present(s.name)) label = s.name;
        }
        if (label.empty()) label = g.season > 0 ? "Season " + std::to_string(g.season) : "Episodes";
    }
    if (!g.dub.empty()) label += (label.empty() ? "" : " · ") + g.dub;
    return label.empty() ? "Episodes" : label;
}

void DetailsScreen::show(Ui &ui) {
    const runtime::Details &d = details_;
    if (present(d.name)) title_->set_text(d.name);
    std::string meta;
    auto add = [&meta](const std::string &s) {
        if (!present(s)) return;
        if (!meta.empty()) meta += "  ·  ";
        meta += s;
    };
    add(d.type);
    if (d.year > 0) add(std::to_string(d.year));
    if (d.duration > 0) add(std::to_string(d.duration) + " min");
    if (present(d.score)) add("★ " + d.score);
    add(d.content_rating);
    add(d.show_status);
    if (d.coming_soon) add("Coming soon");
    meta_->set_text(meta);
    std::string tags;
    for (const auto &t : d.tags) tags += (tags.empty() ? "" : "  ·  ") + t;
    tags_->set_text(tags);
    plot_->set_text(present(d.plot) ? d.plot : std::string());
    const std::string art = !d.background_url.empty() ? d.background_url : !d.poster_url.empty() ? d.poster_url : item_.poster_url;
    backdrop_->set_url(art);

    groups_ = d.episodes.empty() ? decltype(groups_){} : group_episodes(d.episodes);
    const bool episodic = !groups_.empty();
    status_->set_text(d.coming_soon && !episodic && d.data_url.empty() ? "Not available yet." : "");
    status_->set_visible(!status_->text().empty());
    loading_->set_visible(false);
    play_->set_visible(episodic || !d.data_url.empty());

    group_row_->clear_children();
    if (episodic) {
        group_ = groups_.begin()->first;
        if (groups_.size() > 1) {
            for (const auto &kv : groups_) {
                const runtime::EpisodeGroup g = kv.first;
                auto &b = group_row_->add<PillButton>(group_label(g), "", PillButton::Style::Glass, [this, g](Ui &) {
                    show_group(g, 0);
                }, 64);
                b.name = "details.group." + std::to_string(g.season) + g.dub;
                b.set_frosted(true);
            }
        }
        show_group(group_, 0);
    }

    // ResultFragmentTv: the cast only when show_cast_in_details_key is on (the default).
    const bool show_cast = s_.settings->get_bool(settings::kShowCast, true) && !d.actors.empty();
    cast_title_->set_visible(show_cast);
    cast_->set_visible(show_cast);
    cast_->set_items(show_cast ? d.actors.size() : 0, [this](Ui &, size_t i) {
        const auto &a = details_.actors[i];
        auto card = std::make_unique<Card>(a.name, a.image, present(a.role) ? a.role : std::string());
        card->image_aspect = 1.0f;
        return card;
    });

    more_title_->set_visible(!d.recommendations.empty());
    more_->set_visible(!d.recommendations.empty());
    more_->set_items(d.recommendations.size(), [this](Ui &, size_t i) {
        const auto &it = details_.recommendations[i];
        return std::make_unique<Card>(it.name, it.poster_url, it.year > 0 ? std::to_string(it.year) : std::string());
    });
    refresh_library();
    if (resume_on_load_) {
        resume_on_load_ = false;
        if (resume_->visible()) {
            ui.focus(resume_);
            play_resume(ui);
            return;
        }
    }
    if (resume_->visible()) ui.focus(resume_);
    else if (play_->visible()) ui.focus(play_);
    WNK_LOGI(kCat, "details: %s, %zu episodes in %zu groups, %zu recommendations", d.type.c_str(), d.episodes.size(),
             groups_.size(), d.recommendations.size());
}

void DetailsScreen::show_group(const runtime::EpisodeGroup &g, size_t range) {
    group_ = g;
    range_ = range;
    for (auto &child : group_row_->children()) {
        auto *b = static_cast<PillButton *>(child.get());
        b->set_selected(b->text() == group_label(g));
    }
    const auto &eps = groups_[g];
    const auto rs = runtime::episode_ranges(eps);
    range_row_->clear_children();
    if (rs.size() > 1) {
        for (size_t i = 0; i < rs.size(); i++) {
            const std::string text = std::to_string(rs[i].min_episode) + "-" + std::to_string(rs[i].max_episode);
            auto &b = range_row_->add<PillButton>(text, "", PillButton::Style::Glass, [this, g, i](Ui &) { show_group(g, i); }, 64);
            b.set_frosted(true);
            b.set_selected(i == range);
            b.name = "details.range." + std::to_string(i);
        }
    }
    episodes_->clear_children();
    rows_.clear();
    const runtime::EpisodeRange r = range < rs.size() ? rs[range] : runtime::EpisodeRange{0, eps.size(), 0, 0};
    const std::string fallback = !details_.background_url.empty() ? details_.background_url : details_.poster_url;
    for (size_t i = r.start; i < r.start + r.length && i < eps.size(); i++) {
        const runtime::Episode *ep = &eps[i];
        auto &row = episodes_->add<EpisodeRow>(*ep, fallback, [this, g, i](Ui &ui) {
            const auto it = groups_.find(g);
            if (it != groups_.end() && i < it->second.size()) play(ui, &it->second[i]);
        });
        row.set_width(Dim::fill()).set_height(Dim::fixed(200));
        row.name = "details.episode." + std::to_string(i);
        rows_.emplace_back(&row, ep->data);
    }
    if (!eps.empty()) {
        const auto &first = eps[r.start];
        play_->set_text(groups_.size() > 1 || first.season > 0
                            ? "Play S" + std::to_string(first.season) + " E" + std::to_string(first.episode)
                            : "Play E" + std::to_string(first.episode));
    }
    refresh_library();
}

void DetailsScreen::play(Ui &ui, const runtime::Episode *ep) {
    app::PlayTarget t;
    t.provider = provider_;
    t.title = present(details_.name) ? details_.name : item_.name;
    t.poster_url = !details_.poster_url.empty() ? details_.poster_url : item_.poster_url;
    t.details_url = !details_.url.empty() ? details_.url : item_.url;
    t.show = show_info();
    if (ep != nullptr) {
        // The episodes after this one in its group (CloudStream's next episode).
        const auto it = groups_.find(group_);
        if (it != groups_.end()) {
            bool after = false;
            for (const auto &e : it->second) {
                if (after) t.queue.push_back(episode_ref(e));
                if (!after && (&e == ep || e.data == ep->data)) {
                    after = true;
                    t.index = t.episodes.size();
                }
                t.episodes.push_back(episode_ref(e));
            }
        }
        t.data = ep->data;
        t.season = ep->season;
        t.episode = ep->episode;
        t.subtitle = (ep->season > 0 ? "S" + std::to_string(ep->season) + " · " : std::string()) + "E" +
                     std::to_string(ep->episode) + (present(ep->name) ? " " + ep->name : std::string());
    } else {
        t.data = details_.data_url;
    }
    if (t.data.empty()) {
        ui.toast("Nothing to play for this item.");
        return;
    }
    s_.router->open_sources(ui, t);
}

library::Show DetailsScreen::show_info() const {
    library::Show sh;
    sh.provider = provider_;
    sh.url = !details_.url.empty() ? details_.url : item_.url;
    sh.name = present(details_.name) ? details_.name : item_.name;
    sh.type = !details_.type.empty() ? details_.type : item_.type;
    sh.poster_url = !details_.poster_url.empty() ? details_.poster_url : item_.poster_url;
    sh.year = details_.year > 0 ? details_.year : item_.year;
    return sh;
}

library::EpisodeRef DetailsScreen::episode_ref(const runtime::Episode &ep) {
    std::string name = (ep.season > 0 ? "S" + std::to_string(ep.season) + " · " : std::string()) + "E" +
                       std::to_string(ep.episode) + (present(ep.name) ? " " + ep.name : std::string());
    return {ep.data, name, ep.season, ep.episode};
}

void DetailsScreen::refresh_library() {
    const library::Library &lib = s_.library->get();
    const library::Show sh = show_info();
    const library::WatchType wt = lib.watch_type(sh.provider, sh.url);
    bookmark_->set_text(wt == library::WatchType::None ? "Add to library" : library::watch_type_label(wt));
    bookmark_->set_icon(wt == library::WatchType::None ? "bookmark" : "bookmark-filled");
    // Resume: series only (a movie resumes inside the player from its saved position).
    const auto r = loaded_ && !groups_.empty() ? lib.resume(sh.provider, sh.url) : std::nullopt;
    resume_->set_visible(r.has_value());
    if (r) resume_->set_text("Resume " + (r->episode.season > 0 ? "S" + std::to_string(r->episode.season) + " " : std::string()) +
                             "E" + std::to_string(r->episode.episode));
    for (auto &[row, data] : rows_) {
        const auto p = lib.view_pos(provider_, data);
        row->set_progress(p ? library::visual_progress(*p) : 0.0f);
    }
}

void DetailsScreen::play_resume(Ui &ui) {
    const auto r = s_.library->get().resume(provider_, show_info().url);
    if (!r) return;
    for (const auto &kv : groups_) {
        for (const auto &e : kv.second) {
            if (e.data != r->episode.data) continue;
            group_ = kv.first;
            play(ui, &e);
            return;
        }
    }
    ui.toast("That episode is no longer listed by " + provider_ + ".");
}

void DetailsScreen::choose_list(Ui &ui) {
    // CloudStream's bookmark menu: every WatchType, the current one marked, None removes it.
    auto d = std::make_unique<Dialog>("Library");
    Dialog *dlg = d.get();
    const library::Show sh = show_info();
    const library::WatchType cur = s_.library->get().watch_type(sh.provider, sh.url);
    std::vector<library::WatchType> types = library::watch_types();
    types.push_back(library::WatchType::None);
    for (library::WatchType t : types) {
        const std::string text = t == library::WatchType::None ? "Remove from library" : library::watch_type_label(t);
        auto &b = d->body().add<PillButton>(text, t == cur ? "check" : "", PillButton::Style::Glass, [this, dlg, sh, t](Ui &u) {
            s_.library->update([&](library::Library &lib) { lib.set_watch_type(sh, t, library::now_ms()); });
            dlg->dismiss(u);
        }, 60);
        b.name = "library.type." + std::to_string(static_cast<int>(t));
        if (t == library::WatchType::None) b.set_visible(cur != library::WatchType::None);
    }
    d->add_button("Close");
    ui.show_dialog(std::move(d));
}

}  // namespace csweb::ui
