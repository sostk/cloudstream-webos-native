// SPDX-License-Identifier: GPL-3.0-or-later
#include "search/suggestions.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace csweb::search {
namespace {
// SearchSuggestionApi.TMDB_API_KEY: CloudStream's public key for this request.
constexpr const char *kTmdbUrl = "https://api.themoviedb.org/3/search/multi";
constexpr const char *kTmdbKey = "e6333b32409e02a4a6eba6fb7ff866bb";

// HttpUrl's QUERY_COMPONENT_ENCODE_SET for addQueryParameter: unreserved bytes stay, a space becomes
// %20 and everything else (including '+', '&', '=' and non-ASCII bytes) is percent-encoded.
std::string encode_query_component(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string string_or_empty(const nlohmann::json &item, const char *key) {
    auto it = item.find(key);
    return it != item.end() && it->is_string() ? it->get<std::string>() : std::string();
}
}  // namespace

size_t code_points(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

std::string suggestions_url(const std::string &query) {
    // getSuggestions: blank or shorter than two characters asks nothing.
    if (query.find_first_not_of(" \t\r\n") == std::string::npos || code_points(query) < kMinSuggestChars) return {};
    return std::string(kTmdbUrl) + "?api_key=" + kTmdbKey + "&query=" + encode_query_component(query) +
           "&language=en-US";
}

std::vector<std::string> parse_suggestions(const std::string &body) {
    std::vector<std::string> out;
    const auto doc = nlohmann::json::parse(body, nullptr, false);
    if (!doc.is_object()) return out;
    auto results = doc.find("results");
    if (results == doc.end() || !results->is_array()) return out;
    for (const auto &item : *results) {
        if (!item.is_object()) continue;
        const std::string type = string_or_empty(item, "media_type");
        if (type != "movie" && type != "tv") continue;
        auto t = item.find("title");
        // title ?: name: the name when the title is missing or null. An empty title stays empty and
        // is left out (an empty row is no suggestion).
        std::string title = t != item.end() && t->is_string() ? t->get<std::string>() : string_or_empty(item, "name");
        if (title.empty()) continue;
        if (std::find(out.begin(), out.end(), title) != out.end()) continue;  // distinct
        out.push_back(std::move(title));
        if (out.size() == kMaxSuggestions) break;
    }
    return out;
}

const std::vector<std::string> *SuggestionCache::find(const std::string &query) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if (it->first != query) continue;
        items_.splice(items_.begin(), items_, it);
        return &items_.front().second;
    }
    return nullptr;
}

void SuggestionCache::put(const std::string &query, std::vector<std::string> titles) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if (it->first == query) {
            items_.erase(it);
            break;
        }
    }
    items_.emplace_front(query, std::move(titles));
    while (items_.size() > capacity_) items_.pop_back();
}

}  // namespace csweb::search
