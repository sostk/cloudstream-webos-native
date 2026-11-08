// SPDX-License-Identifier: GPL-3.0-or-later
// search/suggestions against CloudStream's SearchSuggestionApi (TMDB multi search).
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "search/suggestions.hpp"

using namespace csweb::search;

TEST("the request is TMDB's multi search with the query encoded as a query parameter") {
    CHECK_EQ(suggestions_url("the office & co+"),
             std::string("https://api.themoviedb.org/3/search/multi?api_key=e6333b32409e02a4a6eba6fb7ff866bb"
                         "&query=the%20office%20%26%20co%2B&language=en-US"));
    CHECK_EQ(suggestions_url("é?"), std::string("https://api.themoviedb.org/3/search/multi?api_key="
                                                 "e6333b32409e02a4a6eba6fb7ff866bb&query=%C3%A9%3F&language=en-US"));
}

TEST("nothing is asked below two characters or for blanks") {
    CHECK(suggestions_url("").empty());
    CHECK(suggestions_url("a").empty());
    CHECK(suggestions_url("é").empty());  // one code point, two bytes
    CHECK(suggestions_url("   ").empty());
    CHECK(!suggestions_url("ab").empty());
}

TEST("movies and shows only, title before name, distinct, ten at most") {
    std::string body = R"({"page":1,"results":[
        {"media_type":"person","name":"Batman Fan"},
        {"media_type":"movie","title":"Batman","original_title":"Batman"},
        {"media_type":"tv","name":"Batman: The Animated Series"},
        {"media_type":"movie","title":"Batman"},
        {"media_type":"tv","title":"Has a title","name":"Not this"},
        {"media_type":"movie"},
        {"media_type":"tv","name":null},
        {"media_type":"movie","title":null,"name":"Null title"}
    ]})";
    const auto s = parse_suggestions(body);
    CHECK_EQ(s.size(), size_t(4));
    CHECK_EQ(s[3], std::string("Null title"));
    CHECK_EQ(s[0], std::string("Batman"));
    CHECK_EQ(s[1], std::string("Batman: The Animated Series"));
    CHECK_EQ(s[2], std::string("Has a title"));

    std::string many = R"({"results":[)";
    for (int i = 0; i < 15; ++i) many += std::string(i ? "," : "") + R"({"media_type":"movie","title":"M)" + std::to_string(i) + "\"}";
    many += "]}";
    CHECK_EQ(parse_suggestions(many).size(), size_t(10));
}

TEST("an unexpected answer gives no suggestions") {
    CHECK(parse_suggestions("").empty());
    CHECK(parse_suggestions("<html>").empty());
    CHECK(parse_suggestions(R"({"status_code":7,"status_message":"Invalid API key"})").empty());
    CHECK(parse_suggestions(R"({"results":{}})").empty());
}

TEST("the cache keeps the newest answers") {
    SuggestionCache c(2);
    c.put("ab", {"A"});
    c.put("abc", {"B"});
    CHECK(c.find("ab") != nullptr);  // now the newest
    c.put("abcd", {"C"});            // drops "abc"
    CHECK(c.find("abc") == nullptr);
    CHECK_EQ(c.find("ab")->front(), std::string("A"));
    c.put("ab", {"A2"});
    CHECK_EQ(c.find("ab")->front(), std::string("A2"));
}
