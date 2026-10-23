// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/settings_screen.hpp"

#include "settings/preferences.hpp"
#include "ui/dialogs/multi_select.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/top_bar.hpp"

#include "wnk/ui/forms.hpp"

#include <algorithm>

#ifndef CSWEB_VERSION
#define CSWEB_VERSION "dev"
#endif

namespace csweb::ui {

using namespace wnk::ui;

namespace {
Column &tab_page(View &screen, app::Services &s, app::Router::Tab tab, const std::string &title) {
    auto &scroll = screen.add<ScrollView>();
    scroll.margin = 160;
    auto &col = scroll.add<Column>(22);
    col.set_padding({theme::kSafeX, 200, theme::kSafeX, 80});
    col.add<Label>(title, TextRole::Title).set_font_size(theme::kDisplay).set_bold(true);
    screen.add<TopBar>(*s.router, tab);
    return col;
}

void section(Column &list, const std::string &title) {
    list.add<Label>(title).set_bold(true).set_color(theme::kAccent).set_padding({0, 24, 0, 0});
}

std::string join(const std::vector<std::string> &v, size_t max) {
    std::string out;
    for (size_t i = 0; i < v.size() && i < max; i++) out += (out.empty() ? "" : ", ") + v[i];
    if (v.size() > max) out += " +" + std::to_string(v.size() - max);
    return out;
}

std::vector<std::string> seek_options() { return {"5 s", "10 s", "15 s", "20 s", "30 s", "45 s", "60 s"}; }
constexpr int kSeekValues[] = {5, 10, 15, 20, 30, 45, 60};
int seek_index(long long seconds) {
    for (int i = 0; i < 7; i++) {
        if (kSeekValues[i] == seconds) return i;
    }
    return 1;
}
constexpr int kSubtitleSizes[] = {75, 100, 125, 150, 175, 200};
}  // namespace

SettingsScreen::SettingsScreen(app::Services &s) : Screen("Settings"), s_(s) {
    name = "settings";
    auto &col = tab_page(*this, s_, app::Router::Tab::Settings, "Settings");
    auto &list = col.add<Column>(12);
    list.set_width(Dim::fixed(1200));
    auto *st = s_.settings;

    first_ = &list.add<ValueRow>("Extensions", "Repositories and plugins", [this](Ui &ui) { s_.router->open_extensions(ui); });
    first_->name = "settings.extensions";

    section(list, "Profiles");
    profiles_ = &list.add<ValueRow>("Profiles", "", [this](Ui &ui) { s_.router->open_profiles(ui); });
    profiles_->name = "settings.profiles";
    list.add<Toggle>("Choose a profile at start", !st->get_bool(settings::kSkipProfileSelect, false), [st](Ui &, bool on) {
        st->set(settings::kSkipProfileSelect, !on);
    }).set_description("With more than one profile; a profile with a PIN always asks").name = "settings.profile_start";

    section(list, "Providers");
    langs_ = &list.add<ValueRow>("Provider languages", "", [this](Ui &ui) { choose_languages(ui); });
    langs_->name = "settings.langs";
    media_ = &list.add<ValueRow>("Preferred media", "", [this](Ui &ui) { choose_media(ui); });
    media_->set_description("Which sources Home and Search use");
    media_->name = "settings.media";
    list.add<ValueRow>("Test extensions", "", [this](Ui &ui) { s_.router->open_provider_tests(ui); }).name = "settings.tests";

    section(list, "Player");
    {
        std::vector<std::string> names;
        int sel = 0;
        const int cur = settings::preferred_max_height(*st);
        for (size_t i = 0; i < settings::quality_choices().size(); i++) {
            const int q = settings::quality_choices()[i];
            names.push_back(runtime::quality_label(q));
            if (q == cur) sel = static_cast<int>(i);
        }
        auto &c = list.add<Choice>("Preferred quality", names, sel, [st](Ui &, int i) {
            st->set(settings::kQualityPref, settings::quality_choices()[static_cast<size_t>(i)]);
        });
        c.set_description("Highest resolution picked from adaptive streams");
        c.name = "settings.quality";
    }
    list.add<Toggle>("Prefer Dolby Vision", settings::prefer_dolby_vision(*st), [st](Ui &, bool on) {
        st->set(settings::kPreferDolbyVision, on);
    }).set_description(s_.device.dolby_vision == true
                           ? "Prioritize Dolby Vision streams (supported by this TV)"
                           : (s_.device.dolby_vision == false
                                  ? "Prioritize Dolby Vision streams (not supported by this TV display)"
                                  : "Prioritize Dolby Vision streams when available"))
      .name = "settings.dolby_vision";
    list.add<Toggle>("Prefer Dolby Atmos", settings::prefer_dolby_atmos(*st), [st](Ui &, bool on) {
        st->set(settings::kPreferDolbyAtmos, on);
    }).set_description(s_.device.dolby_atmos == true
                           ? "Enable immersive audio pass-through (supported by this TV)"
                           : (s_.device.dolby_atmos == false
                                  ? "Enable immersive audio pass-through (not supported by this TV)"
                                  : "Enable immersive audio pass-through when available"))
      .name = "settings.dolby_atmos";
    list.add<Toggle>("Autoplay next episode", st->get_bool(settings::kAutoplayNext, true), [st](Ui &, bool on) {
        st->set(settings::kAutoplayNext, on);
    }).name = "settings.autoplay";
    list.add<Choice>("Seek time with controls shown", seek_options(), seek_index(st->get_int(settings::kSeekOn, 10)),
                     [st](Ui &, int i) { st->set(settings::kSeekOn, kSeekValues[i]); })
        .name = "settings.seek_on";
    list.add<Choice>("Seek time with controls hidden", seek_options(), seek_index(st->get_int(settings::kSeekOff, 10)),
                     [st](Ui &, int i) { st->set(settings::kSeekOff, kSeekValues[i]); })
        .name = "settings.seek_off";

    section(list, "Subtitles");
    {
        std::vector<std::string> names;
        int sel = 0;
        const std::string cur = settings::subtitle_language(*st);
        for (size_t i = 0; i < settings::subtitle_languages().size(); i++) {
            names.push_back(settings::subtitle_languages()[i].name);
            if (settings::subtitle_languages()[i].tag == cur) sel = static_cast<int>(i);
        }
        auto &c = list.add<Choice>("Auto-select language", names, sel, [st](Ui &, int i) {
            st->set(settings::kSubtitleLang, settings::subtitle_languages()[static_cast<size_t>(i)].tag);
        });
        c.set_description("Subtitles in this language turn on when a video has them");
        c.name = "settings.sub_lang";
        std::vector<std::string> sizes;
        int size_sel = 1;
        for (int i = 0; i < 6; i++) {
            sizes.push_back(std::to_string(kSubtitleSizes[i]) + " %");
            if (kSubtitleSizes[i] == st->get_int(settings::kSubtitleSize, 100)) size_sel = i;
        }
        list.add<Choice>("Text size", sizes, size_sel, [st](Ui &, int i) { st->set(settings::kSubtitleSize, kSubtitleSizes[i]); })
            .name = "settings.sub_size";
        list.add<Toggle>("Dark box behind the text", st->get_bool(settings::kSubtitleBox, false), [st](Ui &, bool on) {
            st->set(settings::kSubtitleBox, on);
        }).name = "settings.sub_box";
    }

    section(list, "Layout");
    list.add<Toggle>("Show cast in details", st->get_bool(settings::kShowCast, true), [st](Ui &, bool on) {
        st->set(settings::kShowCast, on);
    }).name = "settings.cast";

    section(list, "Search");
    list.add<ValueRow>("Clear search history", "", [st](Ui &ui) {
        st->set(settings::kSearchHistory, "[]");
        ui.toast("Search history cleared.");
    }).name = "settings.clear_history";

    section(list, "About");
    home_ = &list.add<Label>("");
    home_->set_muted(true);
    runtime_ = &list.add<Label>("");
    runtime_->set_muted(true).set_max_lines(3);
    list.add<Label>(std::string("CSWeb TV ") + CSWEB_VERSION + " · runs CloudStream plugins on this TV").set_muted(true);
    sub_ = s_.plugins_changed.subscribe([this] { refresh(); });
}

void SettingsScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (!shown_) ui.focus(first_);
    shown_ = true;
    refresh();
}

void SettingsScreen::refresh() {
    const settings::Filters f = settings::read_filters(*s_.settings);
    if (f.langs.count(settings::kAllLanguages)) langs_->set_value("All languages");
    else langs_->set_value(join(std::vector<std::string>(f.langs.begin(), f.langs.end()), 4));
    std::vector<std::string> media;
    for (const auto &t : settings::tv_types()) {
        if (f.media.count(t)) media.push_back(t);
    }
    media_->set_value(f.media == settings::default_media() ? "All but NSFW" : media.empty() ? "Any" : join(media, 3));

    profiles_->set_value(s_.profiles.current().name + (s_.profiles.all().size() > 1 ? " · " + std::to_string(s_.profiles.all().size()) + " profiles" : ""));
    const std::string home = s_.settings->get_string(settings::kHomeProvider);
    home_->set_text("Home source: " + (home.empty() ? std::string("none chosen") : home) + " (change it on Home)");
    std::string state;
    switch (s_.runtime->state()) {
        case runtime::RuntimeClient::State::Stopped: state = "stopped"; break;
        case runtime::RuntimeClient::State::Starting: state = "starting"; break;
        case runtime::RuntimeClient::State::Ready: state = "running"; break;
        case runtime::RuntimeClient::State::Failed: state = "failed: " + s_.runtime->last_error(); break;
    }
    runtime_->set_text("Plugin runtime: " + state + " · " + std::to_string(s_.plugins->providers().size()) +
                       " providers loaded");
}

void SettingsScreen::choose_languages(Ui &ui) {
    // SettingsProviders: "All languages" first, then the languages of the loaded providers.
    std::vector<std::string> tags = {settings::kAllLanguages};
    for (const auto &p : s_.plugins->providers()) {
        if (!p.lang.empty() && std::find(tags.begin(), tags.end(), p.lang) == tags.end()) tags.push_back(p.lang);
    }
    std::sort(tags.begin() + 1, tags.end());
    std::vector<std::string> names;
    for (const auto &t : tags) names.push_back(t == settings::kAllLanguages ? "All languages" : t);
    const auto cur = settings::read_filters(*s_.settings).langs;
    std::set<size_t> checked;
    for (size_t i = 0; i < tags.size(); i++) {
        if (cur.count(tags[i])) checked.insert(i);
    }
    show_multi_select(ui, "Provider languages", names, checked, [this, tags](Ui &, const std::set<size_t> &sel) {
        std::set<std::string> out;
        for (size_t i : sel) out.insert(tags[i]);
        settings::put_set(*s_.settings, settings::kProviderLangs, out);
        refresh();
    });
}

void SettingsScreen::choose_media(Ui &ui) {
    // Every TvType, stored as ordinals (prefer_media_type_key_2).
    const auto cur = settings::read_filters(*s_.settings).media;
    std::set<size_t> checked;
    for (size_t i = 0; i < settings::tv_types().size(); i++) {
        if (cur.count(settings::tv_types()[i])) checked.insert(i);
    }
    show_multi_select(ui, "Preferred media", settings::tv_types(), checked, [this](Ui &, const std::set<size_t> &sel) {
        std::set<std::string> out;
        for (size_t i : sel) out.insert(std::to_string(i));
        settings::put_set(*s_.settings, settings::kPreferMediaTypes, out);
        refresh();
    });
}

}  // namespace csweb::ui
