// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings/preferences.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace csweb::settings {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool supports_any(const runtime::Provider &p, const std::set<std::string> &types) {
    return std::any_of(p.types.begin(), p.types.end(), [&](const std::string &t) { return types.count(t) != 0; });
}

// A subset of SubtitleHelper.languages: IETF tag, ISO 639-2/3 codes, English and native names.
struct LanguageData {
    const char *tag;
    std::vector<const char *> codes;
    const char *name;
    const char *native;
};
const std::vector<LanguageData> &language_data() {
    static const std::vector<LanguageData> v = {
        {"ar", {"ara"}, "Arabic", "العربية"},
        {"bn", {"ben"}, "Bengali", "বাংলা"},
        {"bg", {"bul"}, "Bulgarian", "Български"},
        {"zh", {"zho", "chi"}, "Chinese", "中文"},
        {"hr", {"hrv"}, "Croatian", "Hrvatski"},
        {"cs", {"ces", "cze"}, "Czech", "Čeština"},
        {"da", {"dan"}, "Danish", "Dansk"},
        {"nl", {"nld", "dut"}, "Dutch", "Nederlands"},
        {"en", {"eng"}, "English", "English"},
        {"fi", {"fin"}, "Finnish", "Suomi"},
        {"fr", {"fra", "fre"}, "French", "Français"},
        {"de", {"deu", "ger"}, "German", "Deutsch"},
        {"el", {"ell", "gre"}, "Greek", "Ελληνικά"},
        {"he", {"heb"}, "Hebrew", "עברית"},
        {"hi", {"hin"}, "Hindi", "हिन्दी"},
        {"hu", {"hun"}, "Hungarian", "Magyar"},
        {"id", {"ind"}, "Indonesian", "Bahasa Indonesia"},
        {"it", {"ita"}, "Italian", "Italiano"},
        {"ja", {"jpn"}, "Japanese", "日本語"},
        {"ko", {"kor"}, "Korean", "한국어"},
        {"ms", {"msa", "may"}, "Malay", "Bahasa Melayu"},
        {"ne", {"nep"}, "Nepali", "नेपाली"},
        {"no", {"nor", "nob"}, "Norwegian", "Norsk"},
        {"fa", {"fas", "per"}, "Persian", "فارسی"},
        {"pl", {"pol"}, "Polish", "Polski"},
        {"pt", {"por"}, "Portuguese", "Português"},
        {"pt-BR", {"pob", "pb"}, "Portuguese (Brazil)", "Português (Brasil)"},
        {"ro", {"ron", "rum"}, "Romanian", "Română"},
        {"ru", {"rus"}, "Russian", "Русский"},
        {"sr", {"srp"}, "Serbian", "Српски"},
        {"es", {"spa"}, "Spanish", "Español"},
        {"sv", {"swe"}, "Swedish", "Svenska"},
        {"ta", {"tam"}, "Tamil", "தமிழ்"},
        {"te", {"tel"}, "Telugu", "తెలుగు"},
        {"th", {"tha"}, "Thai", "ไทย"},
        {"tr", {"tur"}, "Turkish", "Türkçe"},
        {"uk", {"ukr"}, "Ukrainian", "Українська"},
        {"ur", {"urd"}, "Urdu", "اردو"},
        {"vi", {"vie"}, "Vietnamese", "Tiếng Việt"},
    };
    return v;
}

bool starts_with(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// fromLanguageToTagIETF(code) ?: fromLanguageToTagIETF(name, halfMatch = true)
std::string tag_of(const std::string &field) {
    std::string f = lower(field);
    while (!f.empty() && f.back() == ' ') f.pop_back();
    while (!f.empty() && f.front() == ' ') f.erase(f.begin());
    if (f.size() < 2) return {};
    for (const auto &l : language_data()) {
        if (f == lower(l.tag)) return l.tag;
        for (const char *c : l.codes) {
            if (f == c) return l.tag;
        }
    }
    // A region after the language ("en-US", "pt_br") keeps the language's tag unless one is listed.
    const size_t sep = f.find_first_of("-_");
    if (sep == 2 || sep == 3) {
        const std::string base = f.substr(0, sep);
        for (const auto &l : language_data()) {
            if (base == lower(l.tag)) return l.tag;
        }
    }
    // Names: the longest name the field starts with ("Portuguese (Brazil)" before "Portuguese").
    std::string best;
    size_t best_len = 0;
    for (const auto &l : language_data()) {
        for (const std::string &name : {lower(l.name), lower(l.native)}) {
            if (starts_with(f, name) && name.size() > best_len) {
                best = l.tag;
                best_len = name.size();
            }
        }
    }
    return best;
}

}  // namespace

const std::vector<std::string> &tv_types() {
    static const std::vector<std::string> v = {"Movie",   "AnimeMovie", "TvSeries", "Cartoon",     "Anime",     "OVA",
                                               "Torrent", "Documentary", "AsianDrama", "Live",      "NSFW",      "Others",
                                               "Music",   "AudioBook",  "CustomMedia", "Audio",     "Podcast",   "Video"};
    return v;
}

int tv_type_ordinal(const std::string &name) {
    const auto &v = tv_types();
    const auto it = std::find(v.begin(), v.end(), name);
    return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

const std::vector<TypeChip> &type_chips() {
    static const std::vector<TypeChip> v = {
        {"Movies", {"Movie"}},
        {"TV Series", {"TvSeries"}},
        {"Anime", {"Anime", "OVA", "AnimeMovie"}},
        {"Asian", {"AsianDrama"}},
        {"Cartoons", {"Cartoon"}},
        {"Documentaries", {"Documentary"}},
        {"Livestreams", {"Live"}},
        {"Torrents", {"Torrent"}},
        {"NSFW", {"NSFW"}},
        {"Others", {"Others"}},
    };
    return v;
}

bool chip_visible(const TypeChip &chip, const std::vector<runtime::Provider> &providers) {
    const std::set<std::string> types(chip.types.begin(), chip.types.end());
    return std::any_of(providers.begin(), providers.end(), [&](const runtime::Provider &p) { return supports_any(p, types); });
}

bool chip_selected(const TypeChip &chip, const std::set<std::string> &selected) {
    return std::any_of(chip.types.begin(), chip.types.end(), [&](const std::string &t) { return selected.count(t) != 0; });
}

std::set<std::string> toggle_chip(const TypeChip &chip, const std::set<std::string> &selected) {
    const bool on = !chip_selected(chip, selected);
    std::set<std::string> out;
    for (const auto &c : type_chips()) {
        const bool checked = &c == &chip || c.label == chip.label ? on : chip_selected(c, selected);
        if (checked) out.insert(c.types.begin(), c.types.end());
    }
    return out;
}

std::set<std::string> default_media() {
    std::set<std::string> s(tv_types().begin(), tv_types().end());
    s.erase("NSFW");
    return s;
}

std::set<std::string> get_set(const SettingsStore &s, const std::string &key, const std::set<std::string> &def) {
    const nlohmann::json j = s.get(key);
    if (!j.is_array()) return def;
    std::set<std::string> out;
    for (const auto &v : j) {
        if (v.is_string()) out.insert(v.get<std::string>());
    }
    return out;
}

void put_set(SettingsStore &s, const std::string &key, const std::set<std::string> &values) {
    s.set(key, nlohmann::json(std::vector<std::string>(values.begin(), values.end())));
}

std::set<std::string> default_chip_types() { return {"Movie", "TvSeries"}; }

Filters read_filters(const SettingsStore &s) {
    Filters f;
    f.langs = get_set(s, kProviderLangs, {kAllLanguages});
    if (f.langs.empty()) f.langs = {kAllLanguages};  // getApiProviderLangSettings: empty means the default
    // Stored as ordinals, as CloudStream does; unknown entries are dropped.
    std::set<std::string> media;
    for (const auto &o : get_set(s, kPreferMediaTypes, {"unset"})) {
        if (o == "unset") {
            media = default_media();
            break;
        }
        char *end = nullptr;
        const long n = std::strtol(o.c_str(), &end, 10);
        if (end != o.c_str() && *end == '\0' && n >= 0 && n < static_cast<long>(tv_types().size()))
            media.insert(tv_types()[static_cast<size_t>(n)]);
    }
    f.media = std::move(media);
    return f;
}

bool lang_allowed(const runtime::Provider &p, const std::set<std::string> &langs) {
    return langs.count(kAllLanguages) != 0 || langs.count(p.lang) != 0;
}

std::vector<runtime::Provider> preferred_providers(const std::vector<runtime::Provider> &all, const Filters &f,
                                                   bool home_page_required) {
    std::vector<runtime::Provider> out;
    for (const auto &p : all) {
        if (!lang_allowed(p, f.langs) || (home_page_required && !p.has_main_page)) continue;
        if (!f.media.empty() && !supports_any(p, f.media)) continue;
        out.push_back(p);
    }
    return out;
}

std::vector<std::string> search_providers(const std::vector<runtime::Provider> &all, const Filters &f,
                                          const std::set<std::string> &chosen_types) {
    // SearchFragment: preferred types fall back to the default when the stored set is empty.
    Filters g = f;
    if (g.media.empty()) g.media = default_media();
    const auto valid = preferred_providers(all, g, false);
    std::vector<std::string> chosen, every;
    for (const auto &p : valid) {
        every.push_back(p.name);
        if (supports_any(p, chosen_types)) chosen.push_back(p.name);
    }
    return chosen.empty() ? every : chosen;
}

std::vector<runtime::Provider> home_providers(const std::vector<runtime::Provider> &valid,
                                              const std::set<std::string> &chosen_types) {
    std::vector<runtime::Provider> out;
    for (const auto &p : valid) {
        if (p.has_main_page && supports_any(p, chosen_types)) out.push_back(p);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return lower(a.name) < lower(b.name); });
    return out;
}

const std::vector<int> &quality_choices() {
    static const std::vector<int> v = {2160, 1440, 1080, 720, 480, 360, 240, 144};
    return v;
}

int preferred_max_height(const SettingsStore &s) {
    const long long h = s.get_int(kQualityPref, 2160);
    return h > 0 && h <= 100000 ? static_cast<int>(h) : 2160;
}

bool prefer_dolby_vision(const SettingsStore &s) {
    return s.get_bool(kPreferDolbyVision, true);
}

bool prefer_dolby_atmos(const SettingsStore &s) {
    return s.get_bool(kPreferDolbyAtmos, true);
}

const std::vector<Language> &subtitle_languages() {
    static const std::vector<Language> v = [] {
        std::vector<Language> out = {{"", "None"}};
        for (const auto &l : language_data()) out.push_back({l.tag, l.name});
        return out;
    }();
    return v;
}

std::string subtitle_language(const SettingsStore &s) { return s.get_string(kSubtitleLang, "en"); }

bool subtitle_matches(const std::string &lang_field, const std::string &tag) {
    if (tag.empty()) return false;
    return tag_of(lang_field) == tag;
}

}  // namespace csweb::settings
