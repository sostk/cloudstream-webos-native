// SPDX-License-Identifier: GPL-3.0-or-later
// runtime/runtime_types on events as the runtime prints them (captured from host runs).
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "runtime/runtime_types.hpp"

using namespace csweb::runtime;
using nlohmann::json;

TEST("unescape_url: JSON escapes a plugin left in a URL") {
    // PlayZTV (2026-11-18) gave "https:\/\/<host>\/playlist\/60311\/load-playlist-multi".
    CHECK_EQ(unescape_url(R"(https:\/\/h.example\/playlist\/1\/load)"), std::string("https://h.example/playlist/1/load"));
    CHECK_EQ(unescape_url(R"(https://h.example/v.m3u8?a=1&b=2)"), std::string("https://h.example/v.m3u8?a=1&b=2"));
    CHECK_EQ(unescape_url(R"(http:\/\/h.example\/a=b)"), std::string("http://h.example/a=b"));
    // Unchanged: no escapes, control or non-ASCII escapes, a backslash that is not an escape,
    // and anything that does not become an http(s) URL.
    CHECK_EQ(unescape_url("https://h.example/a%5Cb"), std::string("https://h.example/a%5Cb"));
    CHECK_EQ(unescape_url(R"(https://h.example/a\u000ab)"), std::string(R"(https://h.example/a\u000ab)"));
    CHECK_EQ(unescape_url(R"(https://h.example/aé)"), std::string(R"(https://h.example/aé)"));
    CHECK_EQ(unescape_url(R"(https://h.example/a\b)"), std::string(R"(https://h.example/a\b)"));
    CHECK_EQ(unescape_url(R"(magnet:?xt=urn:btih:abc\/x)"), std::string(R"(magnet:?xt=urn:btih:abc\/x)"));
    CHECK_EQ(unescape_url(""), std::string());
    // Applied where the parsers read URLs.
    auto l = parse_link(json::parse(R"J({"event":"link","name":"N","url":"https:\\/\\/h.example\\/p","referer":"https:\\/\\/r.example\\/",
        "linkType":"VIDEO","audioTracks":[{"url":"https:\\/\\/h.example\\/a.m4a"}],
        "drm":{"uuid":"e2719d58-a985-b3c9-781a-b030af78d30e","licenseUrl":"https:\\/\\/h.example\\/lic"}})J"));
    CHECK_EQ(l.url, std::string("https://h.example/p"));
    CHECK_EQ(l.referer, std::string("https://r.example/"));
    CHECK_EQ(l.audio_tracks.at(0).url, std::string("https://h.example/a.m4a"));
    CHECK_EQ(l.drm->license_url, std::string("https://h.example/lic"));
    auto s = parse_subtitle(json::parse(R"J({"event":"subtitle","lang":"English","url":"https:\\/\\/h.example\\/s.vtt"})J"));
    CHECK_EQ(s.url, std::string("https://h.example/s.vtt"));
}

TEST("provider_and_search") {
    auto p = parse_provider(json::parse(R"J({"event":"provider","name":"Librivox Audiobook","plugin":"/x/L.cs3",
        "mainUrl":"https://librivox.org","lang":"en","hasMainPage":true,"supportedTypes":["Others"],"mainPage":["Latest Audiobook"]})J"));
    CHECK_EQ(p.name, std::string("Librivox Audiobook"));
    CHECK(p.has_main_page);
    CHECK_EQ(p.main_page.size(), size_t(1));
    auto r = parse_search_result(json::parse(R"J({"event":"result","name":"Metropolis","url":"u","apiName":"IA",
        "type":"Movie","posterUrl":"p","year":1927})J"));
    CHECK_EQ(r.year, 1927);
    CHECK_EQ(r.type, std::string("Movie"));
}

TEST("details_full") {
    auto d = parse_details(json::parse(R"J({"event":"details","name":"N","url":"u","type":"TvSeries","year":"2019",
        "score":"8.3","duration":42,"tags":["Drama"],"actors":[{"name":"A","image":null,"role":"MAIN"}],
        "recommendations":[{"name":"R","url":"r"}],"trailers":["t"],"seasons":[{"season":1,"name":"One"}],
        "backgroundPosterUrl":"b","comingSoon":false})J"));
    CHECK_EQ(d.year, 2019);
    CHECK_EQ(d.score, std::string("8.3"));
    CHECK_EQ(d.actors.size(), size_t(1));
    CHECK_EQ(d.actors[0].image, std::string());
    CHECK_EQ(d.recommendations[0].name, std::string("R"));
    CHECK_EQ(d.seasons[0].name, std::string("One"));
    CHECK(d.is_episodic());
    auto m = parse_details(json::parse(R"J({"name":"M","dataUrl":"data"})J"));
    CHECK(!m.is_episodic());
}

TEST("links_and_quality") {
    auto l = parse_link(json::parse(R"J({"event":"link","name":"L","url":"https://x/a.m3u8","quality":1080,
        "linkType":"M3U8","referer":"https://x/","headers":{"User-Agent":"ua"}})J"));
    CHECK_EQ(l.quality, 1080);
    CHECK_EQ(l.headers.at("User-Agent"), std::string("ua"));
    CHECK_EQ(parse_link(json::parse(R"J({"name":"x"})J")).quality, 400);
    CHECK(l.audio_tracks.empty());
    auto v = parse_link(json::parse(R"J({"name":"YouTube H264","url":"https://v/video","quality":720,
        "audioTracks":[{"url":"https://v/a1","headers":{"Referer":"r"}},{"url":""},"bad",{"url":"https://v/a2"}]})J"));
    CHECK_EQ(v.audio_tracks.size(), size_t(2));
    CHECK_EQ(v.audio_tracks[0].headers.at("Referer"), std::string("r"));
    CHECK_EQ(v.audio_tracks[1].url, std::string("https://v/a2"));
    CHECK(!v.drm);
    auto d = parse_link(json::parse(R"J({"name":"D","url":"https://h/m.mpd","linkType":"DASH","drm":{
        "uuid":"e2719d58-a985-b3c9-781a-b030af78d30e","kty":"oct","kid":"_u3wDe7erb7v8Lqt8A3QDQ",
        "key":"ABEiM0RVZneImaq7zN3u_w","licenseUrl":"null","keyRequestParameters":{"a":"b"}}})J"));
    REQUIRE(d.drm.has_value());
    CHECK(d.drm->clearkey());
    CHECK_EQ(d.drm->kid, std::string("_u3wDe7erb7v8Lqt8A3QDQ"));
    CHECK(d.drm->license_url.empty());
    CHECK_EQ(d.drm->key_request_parameters.at("a"), std::string("b"));
    CHECK_EQ(std::string(drm_name("EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED")), std::string("Widevine"));
    CHECK_EQ(std::string(drm_name("9a04f079-9840-4286-ab92-e65be0885f95")), std::string("PlayReady"));
    auto sub = parse_subtitle(json::parse(R"J({"lang":"English","url":"https://s/en.vtt","headers":{"Referer":"https://site/"}})J"));
    CHECK_EQ(sub.headers.at("Referer"), std::string("https://site/"));
    CHECK(parse_subtitle(json::parse(R"J({"lang":"x","url":"u","headerNames":["Referer"]})J")).headers.empty());
    CHECK_EQ(quality_label(1080), std::string("1080p"));
    CHECK_EQ(quality_label(2160), std::string("4K"));
    CHECK_EQ(quality_label(400), std::string());
    CHECK_EQ(quality_label(0), std::string("Auto"));
    auto e = parse_episode(json::parse(R"J({"name":"E","data":"d","season":2,"episode":4,"dub":"DUBBED"})J"));
    CHECK_EQ(e.episode, 4);
    CHECK_EQ(e.dub, std::string("DUBBED"));
}
