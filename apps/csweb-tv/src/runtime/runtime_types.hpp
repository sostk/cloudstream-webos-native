// SPDX-License-Identifier: GPL-3.0-or-later
//
// What plugins return, as plain structs parsed from the runtime's events. Field names follow
// CloudStream's own classes (SearchResponse, LoadResponse, Episode, ExtractorLink, SubtitleFile) so a
// reader can map one to the other; anything a plugin leaves out stays empty or zero.
#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace csweb::runtime {

struct Provider {
    std::string name, plugin, main_url, lang;
    bool has_main_page = false;
    std::vector<std::string> types;      // TvType names: Movie, TvSeries, Anime, Live, Others...
    std::vector<std::string> main_page;  // section names, in order (an index is a "section" request)
    // APIRepository.getMainPage scheduling: sections one after another with a delay, or all at once.
    bool sequential_main_page = false;
    long long sequential_delay_ms = 0, sequential_scroll_delay_ms = 0;
    long long main_page_timeout_ms = 0;  // 0: the app's default
    bool uses_web_view = false, has_download_support = false, instant_link_loading = false;
};

struct SearchResult {
    std::string name, url, api_name, type, poster_url, quality;
    int year = 0;
};

struct Actor {
    std::string name, image, role;
};

struct Episode {
    std::string name, data, poster_url, description, dub, score;
    int season = 0, episode = 0, run_time = 0;
    long long date = 0;
};

struct Season {
    int season = 0;
    std::string name;
};

struct Details {
    std::string name, url, api_name, type, poster_url, background_url, logo_url, plot, score, content_rating,
        show_status, data_url;
    int year = 0, duration = 0;
    bool coming_soon = false;
    std::vector<std::string> tags, trailers;
    std::vector<Actor> actors;
    std::vector<SearchResult> recommendations;
    std::vector<Season> seasons;
    std::vector<Episode> episodes;
    // Movies (and other single-item types) carry the data for loadLinks themselves.
    bool is_episodic() const { return !episodes.empty() || data_url.empty(); }
};

// ExtractorLink.audioTracks entry (AudioFile): an audio stream played together with the link's video.
struct AudioTrack {
    std::string url;
    std::map<std::string, std::string> headers;  // only with withHeaders; never logged
};

// DrmExtractorLink: the DRM system (uuid) and, for ClearKey, the content key as CloudStream's player
// uses it (kid/key, JWK base64url or hex). Keys are secrets: never logged.
struct Drm {
    std::string uuid, kty, kid, key, license_url;
    std::map<std::string, std::string> key_request_parameters;
    bool clearkey() const;  // CLEARKEY_DRM_UUID; Widevine and PlayReady cannot play on this TV
};
const char *drm_name(const std::string &uuid);  // "ClearKey", "Widevine", "PlayReady" or "DRM"

struct Link {
    std::string name, source, url, referer, link_type;  // link_type: VIDEO, M3U8, DASH, TORRENT, MAGNET
    int quality = 400;                                   // CloudStream Qualities value (400 unknown, 0 auto, 1080...)
    std::map<std::string, std::string> headers;          // only with withHeaders; never logged
    std::vector<AudioTrack> audio_tracks;                // separate audio (e.g. YouTube's video-only streams)
    std::optional<Drm> drm;                              // DrmExtractorLink
};

struct Subtitle {
    std::string lang, url;
    std::map<std::string, std::string> headers;  // SubtitleFile.headers (withHeaders); never logged
};

Provider parse_provider(const nlohmann::json &e);
SearchResult parse_search_result(const nlohmann::json &e);
Episode parse_episode(const nlohmann::json &e);
Details parse_details(const nlohmann::json &e);
// A media URL as a plugin gave it, with JSON string escapes the plugin left in undone: some plugins
// copy a URL out of a JSON body with a regex instead of parsing it, so links arrive as
// "https:\/\/host\/path" or with "&" for "&". Only "\/" and "\uXXXX" escapes of printable ASCII
// are undone, and only when the result is an http(s) URL; anything else is returned unchanged.
std::string unescape_url(const std::string &url);

Link parse_link(const nlohmann::json &e);
Subtitle parse_subtitle(const nlohmann::json &e);

// CloudStream Qualities.getStringByInt: "Auto" (0), "" (400, unknown), "4K" (2160), else "<n>p".
std::string quality_label(int quality);

}  // namespace csweb::runtime
