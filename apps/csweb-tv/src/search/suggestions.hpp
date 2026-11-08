// SPDX-License-Identifier: GPL-3.0-or-later
//
// Search suggestions as CloudStream fetches them (ui/search/SearchSuggestionApi.kt): TheMovieDB's
// multi search for what has been typed, keeping the titles of movies and TV shows. The search screen
// asks 300 ms after the last keystroke, from two characters on, when the setting is on (the default).
#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace csweb::search {

inline constexpr size_t kMinSuggestChars = 2;        // query.length < 2: none
inline constexpr int kSuggestDebounceMs = 300;       // SearchViewModel.fetchSuggestions
inline constexpr size_t kMaxSuggestions = 10;        // parseSuggestions: take(10)

// https://api.themoviedb.org/3/search/multi?api_key=…&query=…&language=en-US, the query encoded as
// OkHttp's HttpUrl.addQueryParameter does. Empty when the query is too short to ask.
std::string suggestions_url(const std::string &query);

// SearchSuggestionApi.parseSuggestions: media_type movie or tv, title ?: name, distinct, the first
// ten. Anything unexpected gives an empty list, as CloudStream's catch does.
std::vector<std::string> parse_suggestions(const std::string &body);

// The length in code points (CloudStream counts UTF-16 units; the same for the titles typed here).
size_t code_points(const std::string &s);

// Answers kept for the session (CloudStream caches them a day on disk); the oldest go first.
class SuggestionCache {
 public:
    explicit SuggestionCache(size_t capacity = 64) : capacity_(capacity) {}
    const std::vector<std::string> *find(const std::string &query);
    void put(const std::string &query, std::vector<std::string> titles);

 private:
    size_t capacity_;
    std::list<std::pair<std::string, std::vector<std::string>>> items_;  // most recent first
};

}  // namespace csweb::search
