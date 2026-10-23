// SPDX-License-Identifier: GPL-3.0-or-later
// settings/preferences against CloudStream's defaults and provider filters.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "settings/preferences.hpp"

using namespace csweb::settings;
using csweb::runtime::Provider;

namespace {
Provider prov(const std::string &name, std::vector<std::string> types, std::string lang = "en", bool home = true) {
    Provider p;
    p.name = name;
    p.types = std::move(types);
    p.lang = std::move(lang);
    p.has_main_page = home;
    return p;
}

std::vector<std::string> names(const std::vector<Provider> &v) {
    std::vector<std::string> out;
    for (const auto &p : v) out.push_back(p.name);
    return out;
}
}  // namespace

TEST("tv_type_ordinals_follow_cloudstream") {
    CHECK_EQ(tv_type_ordinal("Movie"), 0);
    CHECK_EQ(tv_type_ordinal("NSFW"), 10);
    CHECK_EQ(tv_type_ordinal("Others"), 11);
    CHECK_EQ(tv_type_ordinal("Video"), 17);
    CHECK_EQ(tv_type_ordinal("Nope"), -1);
    CHECK_EQ(type_chips().size(), size_t(10));
    CHECK_EQ(type_chips()[2].types.size(), size_t(3));  // Anime, OVA, AnimeMovie
}

TEST("defaults_without_stored_settings") {
    wnk::async::WorkerPool pool(1);
    SettingsStore s(nullptr, pool);
    const Filters f = read_filters(s);
    CHECK(f.langs == std::set<std::string>{"universal"});
    CHECK(f.media == default_media());
    CHECK(f.media.count("NSFW") == 0);
    CHECK(f.media.count("Others") == 1);
    CHECK_EQ(preferred_max_height(s), 2160);
    CHECK(prefer_dolby_vision(s) == true);
    CHECK(prefer_dolby_atmos(s) == true);
    CHECK_EQ(subtitle_language(s), std::string("en"));
    CHECK(get_set(s, kSearchTypes, default_chip_types()) == (std::set<std::string>{"Movie", "TvSeries"}));
}

TEST("stored_filters_are_ordinals_and_tags") {
    wnk::async::WorkerPool pool(1);
    SettingsStore s(nullptr, pool);
    s.set(kPreferMediaTypes, nlohmann::json::array({"0", "11", "x", "99"}));
    put_set(s, kProviderLangs, {"fr"});
    const Filters f = read_filters(s);
    CHECK(f.media == (std::set<std::string>{"Movie", "Others"}));
    CHECK(f.langs == std::set<std::string>{"fr"});
    s.set(kPreferMediaTypes, nlohmann::json::array());
    CHECK(read_filters(s).media.empty());  // nothing preferred: no media filter on Home
    put_set(s, kProviderLangs, {});
    CHECK(read_filters(s).langs == std::set<std::string>{"universal"});
}

TEST("dolby_preferences_stored") {
    wnk::async::WorkerPool pool(1);
    SettingsStore s(nullptr, pool);
    s.set(kPreferDolbyVision, false);
    s.set(kPreferDolbyAtmos, false);
    CHECK(prefer_dolby_vision(s) == false);
    CHECK(prefer_dolby_atmos(s) == false);
    s.set(kPreferDolbyVision, true);
    s.set(kPreferDolbyAtmos, true);
    CHECK(prefer_dolby_vision(s) == true);
    CHECK(prefer_dolby_atmos(s) == true);
}

TEST("preferred_providers_filter_language_home_and_media") {
    const std::vector<Provider> all = {prov("Dailymotion", {"Others"}), prov("Archive", {"Movie", "Others"}),
                                       prov("Adult", {"NSFW"}), prov("Francais", {"Movie"}, "fr"),
                                       prov("NoHome", {"Movie"}, "en", false)};
    Filters f;
    f.media = default_media();
    CHECK(names(preferred_providers(all, f, true)) == (std::vector<std::string>{"Dailymotion", "Archive", "Francais"}));
    CHECK_EQ(preferred_providers(all, f, false).size(), size_t(4));
    f.langs = {"en"};
    CHECK(names(preferred_providers(all, f, true)) == (std::vector<std::string>{"Dailymotion", "Archive"}));
    f.media = {"Movie"};
    CHECK(names(preferred_providers(all, f, true)) == (std::vector<std::string>{"Archive"}));
    f.media.clear();
    CHECK_EQ(preferred_providers(all, f, true).size(), size_t(3));  // Adult too: no media filter
}

TEST("search_providers_use_chips_or_fall_back") {
    const std::vector<Provider> all = {prov("Dailymotion", {"Others"}), prov("Archive", {"Movie", "Others"}),
                                       prov("Adult", {"NSFW"})};
    Filters f;
    f.media = default_media();
    CHECK(search_providers(all, f, {"Movie", "TvSeries"}) == std::vector<std::string>{"Archive"});
    // No provider supports a chosen type: every valid provider.
    CHECK(search_providers(all, f, {"Anime"}) == (std::vector<std::string>{"Dailymotion", "Archive"}));
    f.media.clear();  // empty preference falls back to the default for search
    CHECK(search_providers(all, f, {"NSFW"}) == (std::vector<std::string>{"Dailymotion", "Archive"}));
}

TEST("home_providers_sorted_by_lower_case_name") {
    const std::vector<Provider> valid = {prov("beta", {"Movie"}), prov("Alpha", {"Movie"}), prov("Gamma", {"Others"}),
                                         prov("delta", {"Movie"}, "en", false)};
    CHECK(names(home_providers(valid, {"Movie"})) == (std::vector<std::string>{"Alpha", "beta"}));
    CHECK(names(home_providers(valid, {"Others"})) == std::vector<std::string>{"Gamma"});
    CHECK(home_providers(valid, {}).empty());
}

TEST("chips_toggle_and_visibility") {
    const auto &anime = type_chips()[2];
    const auto &movies = type_chips()[0];
    std::set<std::string> sel = {"Movie", "TvSeries"};
    CHECK(chip_selected(movies, sel));
    CHECK(!chip_selected(anime, sel));
    sel = toggle_chip(anime, sel);
    CHECK(sel == (std::set<std::string>{"Movie", "TvSeries", "Anime", "OVA", "AnimeMovie"}));
    sel = toggle_chip(movies, sel);
    CHECK(sel.count("Movie") == 0);
    CHECK(chip_selected(anime, sel));
    const std::vector<Provider> ps = {prov("A", {"OVA"})};
    CHECK(chip_visible(anime, ps));
    CHECK(!chip_visible(movies, ps));
}

TEST("subtitle_language_matching") {
    CHECK(subtitle_matches("en", "en"));
    CHECK(subtitle_matches("eng", "en"));
    CHECK(subtitle_matches("English", "en"));
    CHECK(subtitle_matches("english (CC)", "en"));
    CHECK(subtitle_matches(" EN-us ", "en"));
    CHECK(subtitle_matches("Português", "pt"));
    CHECK(subtitle_matches("Portuguese (Brazil)", "pt-BR"));
    CHECK(!subtitle_matches("Portuguese (Brazil)", "pt"));
    CHECK(subtitle_matches("pob", "pt-BR"));
    CHECK(!subtitle_matches("Spanish", "en"));
    CHECK(!subtitle_matches("e", "en"));
    CHECK(!subtitle_matches("English", ""));
    CHECK_EQ(subtitle_languages().front().name, std::string("None"));
}
