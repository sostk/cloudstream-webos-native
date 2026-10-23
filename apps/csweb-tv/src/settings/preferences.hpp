// SPDX-License-Identifier: GPL-3.0-or-later
//
// CloudStream's user preferences that change what the app shows or plays, with CloudStream's keys
// and defaults, and the provider filters built on them:
//   provider languages   provider_lang_key        AppContextUtils.getApiProviderLangSettings
//   preferred media      prefer_media_type_key_2  filterProviderByPreferredMedia (home and search)
//   search type chips    search_pref_tags         SearchFragment.search
//   home type chips      home_pref_homepage       HomeFragment.selectHomepage
//   preferred quality    quality_pref_key         CS3IPlayer (highest video height for adaptive streams)
//   autoplay next        autoplay_next_key        PlayerView
//   seek time            android_tv_interface_{on,off}_seek_key   FullScreenPlayer
//   show cast            show_cast_in_details_key ResultFragmentTv
//   subtitle language    subs_auto_select         SubtitlesFragment.getAutoSelectLanguageTagIETF
// plus the subtitle look the kit can draw (size and outline or box; CloudStream's SaveCaptionStyle
// has more, which SubtitleView does not support).
#pragma once

#include "runtime/runtime_types.hpp"
#include "settings/settings_store.hpp"

#include <set>
#include <string>
#include <vector>

namespace csweb::settings {

inline constexpr const char *kProviderLangs = "provider_lang_key";
inline constexpr const char *kAllLanguages = "universal";  // AllLanguagesName
inline constexpr const char *kPreferMediaTypes = "prefer_media_type_key_2";
inline constexpr const char *kSearchTypes = "search_pref_tags";
inline constexpr const char *kHomeTypes = "home_pref_homepage";
inline constexpr const char *kQualityPref = "quality_pref_key";
inline constexpr const char *kAutoplayNext = "autoplay_next_key";
inline constexpr const char *kSeekOn = "android_tv_interface_on_seek_key";
inline constexpr const char *kSeekOff = "android_tv_interface_off_seek_key";
inline constexpr const char *kShowCast = "show_cast_in_details_key";
inline constexpr const char *kSubtitleLang = "subs_auto_select";
inline constexpr const char *kSubtitleSize = "csweb_subtitle_size";  // percent of the default size
inline constexpr const char *kSubtitleBox = "csweb_subtitle_box";    // dark box instead of an outline
inline constexpr const char *kPreferDolbyVision = "csweb_prefer_dolby_vision";  // prioritize Dolby Vision streams
inline constexpr const char *kPreferDolbyAtmos = "csweb_prefer_dolby_atmos";    // prioritize Dolby Atmos streams

// TvType names in CloudStream's declaration order (the index is the ordinal).
const std::vector<std::string> &tv_types();
int tv_type_ordinal(const std::string &name);  // -1 when unknown

// The type chips (HomeFragment.getPairList), in CloudStream's order.
struct TypeChip {
    std::string label;
    std::vector<std::string> types;
};
const std::vector<TypeChip> &type_chips();
// A chip shows when a provider supports one of its types.
bool chip_visible(const TypeChip &chip, const std::vector<runtime::Provider> &providers);
bool chip_selected(const TypeChip &chip, const std::set<std::string> &selected);
// The selection after toggling `chip` (CloudStream's chips are checkboxes; the list is every checked
// chip's types).
std::set<std::string> toggle_chip(const TypeChip &chip, const std::set<std::string> &selected);

struct Filters {
    std::set<std::string> langs = {kAllLanguages};
    std::set<std::string> media;  // preferred TvType names; empty: no media filter
};
Filters read_filters(const SettingsStore &s);
// Every TvType but NSFW (the default of prefer_media_type_key_2).
std::set<std::string> default_media();

bool lang_allowed(const runtime::Provider &p, const std::set<std::string> &langs);
// filterProviderByPreferredMedia: language allowed, a home page when required, and (unless no media
// type is preferred) a supported type among the preferred ones.
std::vector<runtime::Provider> preferred_providers(const std::vector<runtime::Provider> &all, const Filters &f,
                                                   bool home_page_required);
// SearchFragment.search: the language and media filtered providers that support one of the chosen
// types, or all of them when none does.
std::vector<std::string> search_providers(const std::vector<runtime::Provider> &all, const Filters &f,
                                          const std::set<std::string> &chosen_types);
// HomeFragment.selectHomepage: providers with a home page supporting one of the chosen types, by
// lower-case name (pinned providers are not built).
std::vector<runtime::Provider> home_providers(const std::vector<runtime::Provider> &valid,
                                              const std::set<std::string> &chosen_types);

// A string set setting (JSON array of strings); `def` when unset or not an array.
std::set<std::string> get_set(const SettingsStore &s, const std::string &key, const std::set<std::string> &def);
void put_set(SettingsStore &s, const std::string &key, const std::set<std::string> &values);
// The chips' default selection (Movie and TvSeries) for kSearchTypes and kHomeTypes.
std::set<std::string> default_chip_types();

// Preferred quality: highest video height, 2160 by default (Qualities.P2160).
const std::vector<int> &quality_choices();  // 2160 … 144, as CloudStream lists them
int preferred_max_height(const SettingsStore &s);
bool prefer_dolby_vision(const SettingsStore &s);
bool prefer_dolby_atmos(const SettingsStore &s);

// Subtitle languages offered for auto-select: IETF tag and English name. The first entry is "None" ("").
struct Language {
    std::string tag, name;
};
const std::vector<Language> &subtitle_languages();
std::string subtitle_language(const SettingsStore &s);  // "en" by default, "" for none
// SubtitleData.matchesLanguageCode: the IETF tag of a plugin's subtitle language field ("en",
// "eng", "English", "English (CC)", "Português") equals `tag`. Names match on their start.
bool subtitle_matches(const std::string &lang_field, const std::string &tag);

}  // namespace csweb::settings
