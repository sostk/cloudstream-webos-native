// SPDX-License-Identifier: GPL-3.0-or-later
// plugins/load_hints: what is remembered between starts to load the Home source first and to skip
// a plugin's failed Context-only load.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "plugins/load_hints.hpp"

using namespace csweb::plugins;

namespace {
InstalledPlugin plugin(const std::string &id, const std::string &sha) {
    InstalledPlugin p;
    p.id = id;
    p.internal_name = id;
    p.sha256 = sha;
    return p;
}
}  // namespace

TEST("hints survive a round trip and belong to one build") {
    LoadHints h;
    h["a"] = {"aaa", {"DaddyLive"}, ""};
    h["b"] = {"bbb", {"⚡Cricify Live Events", "Cricify"}, "androidx.appcompat.app.AppCompatActivity"};
    const LoadHints back = parse_load_hints(dump_load_hints(h));
    CHECK_EQ(back.size(), size_t(2));
    CHECK_EQ(back.at("b").activity, std::string("androidx.appcompat.app.AppCompatActivity"));
    CHECK_EQ(back.at("b").providers.size(), size_t(2));
    CHECK(hint_for(back, plugin("b", "bbb")) != nullptr);
    CHECK(hint_for(back, plugin("b", "updated")) == nullptr);  // a new build learns again
    CHECK(hint_for(back, plugin("c", "ccc")) == nullptr);
}

TEST("bad or hostile hint data is ignored") {
    CHECK(parse_load_hints("").empty());
    CHECK(parse_load_hints("[1,2]").empty());
    const LoadHints h = parse_load_hints(R"({"x":{"providers":["P"]},"y":{"sha256":"s","activity":"a b;rm"}})");
    CHECK(h.count("x") == 0);                // no hash: not tied to a build
    CHECK_EQ(h.at("y").activity, std::string());  // not a class name
    CHECK(valid_activity_name("androidx.fragment.app.FragmentActivity"));
    CHECK(valid_activity_name("com.lagradost.cloudstream3.MainActivity"));
    CHECK(!valid_activity_name("../x"));
    CHECK(!valid_activity_name("a..b"));
    CHECK(!valid_activity_name(""));
}

TEST("the plugin behind the Home source loads first, the rest keep their order") {
    const std::vector<InstalledPlugin> installed = {plugin("daddy", "1"), plugin("cricify", "2"), plugin("yt", "3")};
    LoadHints h;
    h["daddy"] = {"1", {"DaddyLive"}, ""};
    h["cricify"] = {"2", {"⚡Cricify Live Events"}, "androidx.appcompat.app.AppCompatActivity"};
    CHECK(load_order(installed, h, "⚡Cricify Live Events") == std::vector<size_t>({1, 0, 2}));
    CHECK(load_order(installed, h, "") == std::vector<size_t>({0, 1, 2}));
    CHECK(load_order(installed, h, "Unknown") == std::vector<size_t>({0, 1, 2}));
    h["cricify"].sha256 = "old";  // updated since: no reordering on stale knowledge
    CHECK(load_order(installed, h, "⚡Cricify Live Events") == std::vector<size_t>({0, 1, 2}));
}
