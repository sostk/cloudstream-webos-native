// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/settings_screen.hpp"

#include "runtime/network_settings.hpp"
#include "net/proxy_pool.hpp"
#include "settings/preferences.hpp"
#include "settings/proxy_rules.hpp"
#include "ui/dialogs/clear_watch_history.hpp"
#include "ui/dialogs/multi_select.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/glass_dialog.hpp"
#include "ui/widgets/top_bar.hpp"

#include "wnk/ui/forms.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

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
    screen.add<TopBar>(*s.router, tab).follow(&scroll);
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

// Proxy countries: every country ProxyScrape's free list had proxies for on 2026-11-23 (its
// proxies/countries/<code>/ files), by name. A country that has none at the moment reports that.
struct ProxyCountry {
    const char *name, *code;
};
constexpr ProxyCountry kProxyCountries[] = {
    {"Albania", "al"}, {"Argentina", "ar"}, {"Australia", "au"}, {"Austria", "at"}, {"Bangladesh", "bd"},
    {"Belgium", "be"}, {"Benin", "bj"}, {"Bolivia", "bo"}, {"Brazil", "br"}, {"Bulgaria", "bg"}, {"Cambodia", "kh"},
    {"Cameroon", "cm"}, {"Canada", "ca"}, {"Chile", "cl"}, {"China", "cn"}, {"Colombia", "co"}, {"Cyprus", "cy"},
    {"Czechia", "cz"}, {"Denmark", "dk"}, {"Dominican Republic", "do"}, {"Ecuador", "ec"}, {"Egypt", "eg"},
    {"Finland", "fi"}, {"France", "fr"}, {"Georgia", "ge"}, {"Germany", "de"}, {"Greece", "gr"}, {"Guatemala", "gt"},
    {"Honduras", "hn"}, {"Hong Kong", "hk"}, {"Hungary", "hu"}, {"India", "in"}, {"Indonesia", "id"}, {"Iran", "ir"},
    {"Iraq", "iq"}, {"Italy", "it"}, {"Japan", "jp"}, {"Kazakhstan", "kz"}, {"Kenya", "ke"}, {"Kyrgyzstan", "kg"},
    {"Latvia", "lv"}, {"Lebanon", "lb"}, {"Libya", "ly"}, {"Lithuania", "lt"}, {"Malawi", "mw"}, {"Malaysia", "my"},
    {"Mexico", "mx"}, {"Mongolia", "mn"}, {"Nepal", "np"}, {"Nigeria", "ng"}, {"Pakistan", "pk"},
    {"Palestine", "ps"}, {"Panama", "pa"}, {"Paraguay", "py"}, {"Peru", "pe"}, {"Philippines", "ph"},
    {"Poland", "pl"}, {"Puerto Rico", "pr"}, {"Qatar", "qa"}, {"Romania", "ro"}, {"Russia", "ru"},
    {"Saudi Arabia", "sa"}, {"Serbia", "rs"}, {"Seychelles", "sc"}, {"Singapore", "sg"}, {"South Africa", "za"},
    {"South Korea", "kr"}, {"Spain", "es"}, {"Sweden", "se"}, {"Switzerland", "ch"}, {"Syria", "sy"},
    {"Taiwan", "tw"}, {"Thailand", "th"}, {"The Netherlands", "nl"}, {"Turkey", "tr"}, {"Ukraine", "ua"},
    {"United Arab Emirates", "ae"}, {"United Kingdom", "gb"}, {"United States", "us"}, {"Uruguay", "uy"},
    {"Uzbekistan", "uz"}, {"Venezuela", "ve"}, {"Vietnam", "vn"},
};
const char *country_name(const std::string &code) {
    for (const auto &c : kProxyCountries) {
        if (code == c.code) return c.name;
    }
    return "?";
}

std::string rule_label(const settings::ProxyRule &r) {
    switch (r.type) {
        case settings::ProxyRule::Type::Free: return std::string("Free proxy · ") + country_name(r.country);
        case settings::ProxyRule::Type::Personal: return "My proxy · " + net::proxy_label(r.url);
        case settings::ProxyRule::Type::Off: break;
    }
    return "Direct";
}

// user:password for a proxy URL, each part percent-encoded (':' and '@' are separators there).
std::string url_login(const std::string &user, const std::string &password) {
    auto enc = [](const std::string &v) {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : v) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    };
    if (user.empty() && password.empty()) return {};
    return enc(user) + (password.empty() ? "" : ":" + enc(password));
}
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

    section(list, "Network");
    {
        // SettingsGeneral: "DNS over HTTPS — Useful for bypassing ISP blocks"; applied at once (initClient).
        std::vector<std::string> names;
        for (const auto &d : settings::dns_choices()) names.push_back(d.name);
        auto &c = list.add<Choice>("DNS over HTTPS", names, settings::dns_index(settings::dns_value(*st)),
                                   [this, st](Ui &, int i) {
                                       const int v = settings::dns_choices()[static_cast<size_t>(i)].value;
                                       st->set(settings::kDns, v);
                                       runtime::send_dns(*s_.runtime, v);
                                   });
        c.set_description("Useful for bypassing ISP blocks. Applies to plugins' requests");
        c.name = "settings.dns";
    }

    // Not in CloudStream: videos some services send only to viewers in their country (JioTV: HTTP 451
    // elsewhere) go through a proxy there, per provider: a free public one or the user's own.
    proxies_ = &list.add<ValueRow>("Proxies", "", [this](Ui &ui) { open_proxies(ui); });
    proxies_->set_description("Per provider: send its videos through a free proxy in a chosen country, or your own "
                              "proxy server. Free proxies are slow and change often, and their operators can see "
                              "which sites you reach");
    proxies_->name = "settings.proxies";

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
    // SettingsUIScreen: "Search Suggestions — Show search suggestions while typing", on by default.
    list.add<Toggle>("Search suggestions while typing", st->get_bool(settings::kSearchSuggestions, true),
                     [st](Ui &, bool on) { st->set(settings::kSearchSuggestions, on); })
        .name = "settings.search_suggestions";
    list.add<ValueRow>("Clear watch history", "Continue Watching", [this](Ui &ui) {
        confirm_clear_watch_history(ui, s_);
    }).name = "settings.clear_watch_history";

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

    {
        const settings::ProxyRules rules = settings::read_proxy_rules(*s_.settings);
        if (rules.empty()) proxies_->set_value("Off");
        else if (rules.size() == 1) proxies_->set_value(rules.begin()->first + " · " + rule_label(rules.begin()->second));
        else proxies_->set_value(std::to_string(rules.size()) + " providers");
    }
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

void SettingsScreen::open_proxies(Ui &ui, const std::string &focus) {
    // Every loaded provider, plus any with a rule whose plugin is not loaded now (kept).
    std::vector<std::string> names;
    for (const auto &p : s_.plugins->providers()) {
        if (std::find(names.begin(), names.end(), p.name) == names.end()) names.push_back(p.name);
    }
    const settings::ProxyRules rules = settings::read_proxy_rules(*s_.settings);
    for (const auto &[name, r] : rules) {
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    auto d = std::make_unique<GlassDialog>("Proxies", "Choose a provider to change how its videos are fetched.");
    Dialog *dlg = d.get();
    auto &scroll = d->body().add<ScrollView>();
    scroll.set_height(Dim::fixed(names.size() > 7 ? 560.0f : std::max<size_t>(names.size(), 1) * 80.0f));
    scroll.set_width(Dim::fixed(GlassDialog::kContentWidth));
    auto &list = scroll.add<Column>(6);
    View *first = nullptr, *focused = nullptr;
    if (names.empty()) list.add<Label>("No providers are loaded.");
    for (const auto &name : names) {
        const auto it = rules.find(name);
        auto &row = list.add<ValueRow>(name, it == rules.end() ? "Direct" : rule_label(it->second), [this, dlg, name](Ui &u) {
            dlg->dismiss(u);
            open_proxy_rule(u, name);
        });
        row.name = "proxies." + name;
        if (!first) first = &row;
        if (name == focus) focused = &row;
    }
    d->add_button("Done");
    ui.show_dialog(std::move(d));
    if (focused) ui.focus(focused);
    else if (first) ui.focus(first);
}

void SettingsScreen::open_proxy_rule(Ui &ui, const std::string &provider) {
    const settings::ProxyRule cur = settings::proxy_rule_for(*s_.settings, provider);
    auto d = std::make_unique<GlassDialog>(provider, "Now: " + rule_label(cur) + ".");
    auto apply = [this, provider](Ui &u, const settings::ProxyRule &r) {
        settings::set_proxy_rule(*s_.settings, provider, r);
        refresh();
        open_proxies(u, provider);
    };
    // Dialog buttons dismiss the dialog before their handler runs.
    d->add_button("Direct", [apply](Ui &u) { apply(u, settings::ProxyRule{}); }).name = "proxy.direct";
    d->add_button("Free proxy", [this, provider, cur, apply](Ui &u) {
        std::vector<std::string> names;
        size_t sel = 0;
        for (size_t i = 0; i < std::size(kProxyCountries); i++) {
            names.push_back(kProxyCountries[i].name);
            if (cur.country == kProxyCountries[i].code) sel = i;
        }
        show_single_select(u, provider + ": proxy country", names, sel, [apply](Ui &v, size_t i) {
            settings::ProxyRule r;
            r.type = settings::ProxyRule::Type::Free;
            r.country = kProxyCountries[i].code;
            apply(v, r);
        });
    }).name = "proxy.free";
    d->add_button("My proxy", [this, provider, cur](Ui &u) { open_personal_proxy(u, provider, cur); }).name = "proxy.mine";
    d->add_button("Cancel", [this, provider](Ui &u) { open_proxies(u, provider); });
    ui.show_dialog(std::move(d));
}

void SettingsScreen::open_personal_proxy(Ui &ui, const std::string &provider, const settings::ProxyRule &cur) {
    auto d = std::make_unique<GlassDialog>(provider + ": my proxy",
                                           "The proxy's address with its port, like socks5://1.2.3.4:1080 or "
                                           "1.2.3.4:8080 (HTTP). Login only if it needs one.");
    Dialog *dlg = d.get();
    auto &addr = d->body().add<TextField>("socks5://host:port or host:port");
    addr.set_width(Dim::fixed(800));
    addr.name = "proxy.mine.address";
    if (cur.type == settings::ProxyRule::Type::Personal) {
        // The saved address without its login (the login fields start empty; leaving them empty
        // keeps the saved login).
        std::string scheme = cur.url.substr(0, cur.url.find("://") + 3);
        addr.set_text(scheme + net::proxy_label(cur.url));
    }
    auto &user = d->body().add<TextField>("Username (optional)");
    user.set_width(Dim::fixed(800));
    user.name = "proxy.mine.user";
    auto &pass = d->body().add<TextField>("Password (optional)");
    pass.set_width(Dim::fixed(800));
    pass.set_password(true);
    pass.name = "proxy.mine.password";
    TextField *a = &addr, *us = &user, *pw = &pass;
    auto save = [this, provider, cur, a, us, pw](Ui &u) {
        std::string typed = a->text();
        std::string login = url_login(us->text(), pw->text());
        if (login.empty() && cur.type == settings::ProxyRule::Type::Personal) {
            const size_t s = cur.url.find("://"), at = cur.url.rfind('@');
            if (s != std::string::npos && at != std::string::npos && at > s) login = cur.url.substr(s + 3, at - s - 3);
        }
        if (!login.empty()) {
            const size_t s = typed.find("://");
            typed = s == std::string::npos ? login + "@" + typed : typed.substr(0, s + 3) + login + "@" + typed.substr(s + 3);
        }
        std::string url, error;
        if (!settings::normalize_proxy_url(typed, &url, &error)) {
            u.toast(error, 4000);
            open_personal_proxy(u, provider, cur);
            return;
        }
        settings::ProxyRule r;
        r.type = settings::ProxyRule::Type::Personal;
        r.url = url;
        settings::set_proxy_rule(*s_.settings, provider, r);
        refresh();
        u.toast("Saved: " + provider + " uses " + net::proxy_label(url), 2500);
        open_proxies(u, provider);
    };
    addr.set_on_submit([us](Ui &u, const std::string &) { u.focus(us); });
    user.set_on_submit([pw](Ui &u, const std::string &) { u.focus(pw); });
    pass.set_on_submit([save, dlg](Ui &u, const std::string &) {
        dlg->dismiss(u);
        save(u);
    });
    d->add_button("Cancel", [this, provider](Ui &u) { open_proxies(u, provider); });
    d->add_button("Save", save).name = "proxy.mine.save";
    ui.show_dialog(std::move(d));
    ui.focus(a);
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
