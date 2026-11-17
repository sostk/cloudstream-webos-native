// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/runtime_types.hpp"

#include <cctype>

namespace csweb::runtime {

namespace {

using nlohmann::json;

std::string str(const json &e, const char *key) {
    auto it = e.find(key);
    if (it == e.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

long long num(const json &e, const char *key, long long def = 0) {
    auto it = e.find(key);
    if (it == e.end()) return def;
    if (it->is_number_integer()) return it->get<long long>();
    if (it->is_number()) return static_cast<long long>(it->get<double>());
    if (it->is_string()) {  // the runtime sends some numbers as strings (String.valueOf)
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

// A boolean the runtime may send as null (an unset Kotlin property): absent or null is false.
bool flag(const json &e, const char *key) {
    auto it = e.find(key);
    return it != e.end() && it->is_boolean() && it->get<bool>();
}

std::vector<std::string> strings(const json &e, const char *key) {
    std::vector<std::string> out;
    auto it = e.find(key);
    if (it == e.end() || !it->is_array()) return out;
    for (const auto &v : *it) {
        if (v.is_string()) out.push_back(v.get<std::string>());
    }
    return out;
}

}  // namespace

Provider parse_provider(const json &e) {
    Provider p;
    p.name = str(e, "name");
    p.plugin = str(e, "plugin");
    p.main_url = str(e, "mainUrl");
    p.lang = str(e, "lang");
    p.has_main_page = flag(e, "hasMainPage");
    p.types = strings(e, "supportedTypes");
    p.main_page = strings(e, "mainPage");
    p.sequential_main_page = flag(e, "sequentialMainPage");
    p.sequential_delay_ms = num(e, "sequentialMainPageDelay");
    p.sequential_scroll_delay_ms = num(e, "sequentialMainPageScrollDelay");
    p.main_page_timeout_ms = num(e, "mainPageTimeoutMs");
    p.uses_web_view = flag(e, "usesWebView");
    p.has_download_support = flag(e, "hasDownloadSupport");
    p.instant_link_loading = flag(e, "instantLinkLoading");
    return p;
}

SearchResult parse_search_result(const json &e) {
    SearchResult r;
    r.name = str(e, "name");
    r.url = str(e, "url");
    r.api_name = str(e, "apiName");
    r.type = str(e, "type");
    r.poster_url = str(e, "posterUrl");
    r.quality = str(e, "quality");
    r.year = static_cast<int>(num(e, "year"));
    return r;
}

Episode parse_episode(const json &e) {
    Episode ep;
    ep.name = str(e, "name");
    ep.data = str(e, "data");
    ep.poster_url = str(e, "posterUrl");
    ep.description = str(e, "description");
    ep.dub = str(e, "dub");
    ep.score = str(e, "score");
    ep.season = static_cast<int>(num(e, "season"));
    ep.episode = static_cast<int>(num(e, "episode"));
    ep.run_time = static_cast<int>(num(e, "runTime"));
    ep.date = num(e, "date");
    return ep;
}

Details parse_details(const json &e) {
    Details d;
    d.name = str(e, "name");
    d.url = str(e, "url");
    d.api_name = str(e, "apiName");
    d.type = str(e, "type");
    d.poster_url = str(e, "posterUrl");
    d.background_url = str(e, "backgroundPosterUrl");
    d.logo_url = str(e, "logoUrl");
    d.plot = str(e, "plot");
    d.score = str(e, "score");
    d.content_rating = str(e, "contentRating");
    d.show_status = str(e, "showStatus");
    d.data_url = str(e, "dataUrl");
    d.year = static_cast<int>(num(e, "year"));
    d.duration = static_cast<int>(num(e, "duration"));
    d.coming_soon = flag(e, "comingSoon");
    d.tags = strings(e, "tags");
    if (auto it = e.find("trailers"); it != e.end() && it->is_array()) {
        for (const auto &t : *it) {
            if (t.is_string()) d.trailers.push_back(t.get<std::string>());
        }
    }
    if (auto it = e.find("actors"); it != e.end() && it->is_array()) {
        for (const auto &a : *it) {
            if (a.is_object()) d.actors.push_back({str(a, "name"), str(a, "image"), str(a, "role")});
        }
    }
    if (auto it = e.find("recommendations"); it != e.end() && it->is_array()) {
        for (const auto &r : *it) {
            if (r.is_object()) d.recommendations.push_back(parse_search_result(r));
        }
    }
    if (auto it = e.find("seasons"); it != e.end() && it->is_array()) {
        for (const auto &s : *it) {
            if (!s.is_object()) continue;
            Season season;
            season.season = static_cast<int>(num(s, "season"));
            season.name = str(s, "name");
            d.seasons.push_back(season);
        }
    }
    return d;
}

std::string unescape_url(const std::string &url) {
    if (url.find('\\') == std::string::npos) return url;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(url.size());
    for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '\\' && i + 1 < url.size() && url[i + 1] == '/') {
            out += '/';
            ++i;
            continue;
        }
        if (url[i] == '\\' && i + 5 < url.size() && url[i + 1] == 'u') {
            int v = 0;
            bool ok = true;
            for (size_t k = 2; k < 6 && ok; ++k) {
                const int h = hex(url[i + k]);
                ok = h >= 0;
                v = v * 16 + h;
            }
            if (ok && v >= 0x21 && v < 0x7f) {
                out += static_cast<char>(v);
                i += 5;
                continue;
            }
        }
        out += url[i];
    }
    const bool web = out.rfind("http://", 0) == 0 || out.rfind("https://", 0) == 0;
    return web ? out : url;
}

Link parse_link(const json &e) {
    Link l;
    l.name = str(e, "name");
    l.source = str(e, "source");
    l.url = unescape_url(str(e, "url"));
    l.referer = unescape_url(str(e, "referer"));
    l.link_type = str(e, "linkType");
    l.quality = static_cast<int>(num(e, "quality", 400));
    if (auto it = e.find("headers"); it != e.end() && it->is_object()) {
        for (auto h = it->begin(); h != it->end(); ++h) {
            if (h.value().is_string()) l.headers[h.key()] = h.value().get<std::string>();
        }
    }
    if (auto it = e.find("drm"); it != e.end() && it->is_object()) {
        Drm d;
        d.uuid = str(*it, "uuid");
        d.kty = str(*it, "kty");
        d.kid = str(*it, "kid");
        d.key = str(*it, "key");
        d.license_url = unescape_url(str(*it, "licenseUrl"));
        if (d.license_url == "null") d.license_url.clear();
        if (auto p = it->find("keyRequestParameters"); p != it->end() && p->is_object()) {
            for (auto v = p->begin(); v != p->end(); ++v) {
                if (v.value().is_string()) d.key_request_parameters[v.key()] = v.value().get<std::string>();
            }
        }
        l.drm = std::move(d);
    }
    if (auto it = e.find("audioTracks"); it != e.end() && it->is_array()) {
        for (const auto &t : *it) {
            if (!t.is_object() || str(t, "url").empty()) continue;
            AudioTrack a;
            a.url = unescape_url(str(t, "url"));
            if (auto h = t.find("headers"); h != t.end() && h->is_object()) {
                for (auto v = h->begin(); v != h->end(); ++v) {
                    if (v.value().is_string()) a.headers[v.key()] = v.value().get<std::string>();
                }
            }
            l.audio_tracks.push_back(std::move(a));
        }
    }
    return l;
}

namespace {
std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

// CloudStream's CLEARKEY_DRM_UUID, WIDEVINE_DRM_UUID and PLAYREADY_DRM_UUID (ExtractorApi.kt).
const char *drm_name(const std::string &uuid) {
    const std::string u = lower(uuid);
    if (u == "e2719d58-a985-b3c9-781a-b030af78d30e") return "ClearKey";
    if (u == "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed") return "Widevine";
    if (u == "9a04f079-9840-4286-ab92-e65be0885f95") return "PlayReady";
    return "DRM";
}

bool Drm::clearkey() const { return std::string(drm_name(uuid)) == "ClearKey"; }

Subtitle parse_subtitle(const json &e) {
    Subtitle s{str(e, "lang"), unescape_url(str(e, "url")), {}};
    if (auto it = e.find("headers"); it != e.end() && it->is_object()) {
        for (auto h = it->begin(); h != it->end(); ++h) {
            if (h.value().is_string()) s.headers[h.key()] = h.value().get<std::string>();
        }
    }
    return s;
}

std::string quality_label(int quality) {
    if (quality == 0) return "Auto";
    if (quality == 400) return {};
    if (quality == 2160) return "4K";
    return std::to_string(quality) + "p";
}

}  // namespace csweb::runtime
