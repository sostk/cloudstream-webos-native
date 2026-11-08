// SPDX-License-Identifier: GPL-3.0-or-later
// library/library against DataStoreHelper's rules (setViewPos, setViewPosAndResume, getPos, fixVisual).
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "library/library.hpp"

using namespace csweb::library;

namespace {
Show show() { return {"Prov", "https://s/1", "Show", "TvSeries", "p", 2020}; }
EpisodeRef ep(int n) { return {"data" + std::to_string(n), "E" + std::to_string(n), 1, n}; }
}  // namespace

TEST("bookmarks") {
    Library lib;
    lib.set_watch_type(show(), WatchType::PlanToWatch, 10);
    CHECK(lib.watch_type("Prov", "https://s/1") == WatchType::PlanToWatch);
    lib.set_watch_type(show(), WatchType::Watching, 20);
    auto w = lib.bookmarks(WatchType::Watching);
    REQUIRE(w.size() == 1);
    CHECK_EQ(w[0].bookmarked_ms, int64_t(10));  // first bookmark time kept
    CHECK(lib.bookmarks(WatchType::PlanToWatch).empty());
    lib.set_watch_type(show(), WatchType::None, 30);
    CHECK(lib.watch_type("Prov", "https://s/1") == WatchType::None);
}

TEST("positions_and_resume") {
    Library lib;
    lib.set_view_pos_and_resume(show(), ep(1), ep(2), 10000, 20000, 1);  // under 30 s: no position
    CHECK(!lib.view_pos("Prov", "data1"));
    REQUIRE(lib.resume("Prov", "https://s/1").has_value());
    lib.set_view_pos_and_resume(show(), ep(1), ep(2), 600000, 1200000, 2);
    CHECK_EQ(lib.start_position_ms("Prov", "data1"), int64_t(600000));
    {
        const auto r = lib.resume("Prov", "https://s/1");  // CHECK_EQ keeps a reference: not into a temporary
        REQUIRE(r.has_value());
        CHECK_EQ(r->episode.episode, 1);
    }
    lib.set_view_pos_and_resume(show(), ep(1), ep(2), 1100000, 1200000, 3);  // 91 %: next becomes resume
    {
        const auto r = lib.resume("Prov", "https://s/1");  // CHECK_EQ keeps a reference: not into a temporary
        REQUIRE(r.has_value());
        CHECK_EQ(r->episode.episode, 2);
    }
    lib.set_view_pos_and_resume(show(), ep(1), ep(2), 1160000, 1200000, 4);  // 96 %: starts over
    CHECK_EQ(lib.start_position_ms("Prov", "data1"), int64_t(0));
    lib.set_view_pos_and_resume(show(), ep(2), std::nullopt, 1150000, 1200000, 5);  // last one done
    CHECK(!lib.resume("Prov", "https://s/1"));
}

TEST("json_round_trip") {
    Library lib;
    lib.set_watch_type(show(), WatchType::Completed, 7);
    lib.set_view_pos_and_resume(show(), ep(3), ep(4), 100000, 1000000, 8);
    Library back = Library::from_json(nlohmann::json::parse(lib.to_json().dump()));
    CHECK(back.watch_type("Prov", "https://s/1") == WatchType::Completed);
    CHECK_EQ(back.start_position_ms("Prov", "data3"), int64_t(100000));
    const auto resume = back.resume_list();  // CHECK_EQ keeps a reference: not into a temporary
    REQUIRE(resume.size() == 1);
    CHECK_EQ(resume[0].episode.data, std::string("data3"));
    CHECK(Library::from_json(nlohmann::json("garbage")).resume_list().empty());
}

TEST("visual_progress") {
    CHECK_EQ(visual_progress({5, 1000, 0}), 0.0f);
    CHECK_EQ(visual_progress({40, 1000, 0}), 0.05f);
    CHECK_EQ(visual_progress({960, 1000, 0}), 1.0f);
    CHECK_EQ(visual_progress({500, 1000, 0}), 0.5f);
}

TEST("local_list_sortings") {
    using namespace csweb::library;
    auto bm = [](const std::string &name, int year, int64_t updated) {
        Bookmark b;
        b.show.name = name;
        b.show.year = year;
        b.updated_ms = updated;
        return b;
    };
    const std::vector<Bookmark> base = {bm("beta", 2001, 30), bm("Alpha", 1999, 10), bm("Gamma", 2010, 20)};
    auto names = [](const std::vector<Bookmark> &v) {
        std::string s;
        for (const auto &b : v) s += b.show.name + ",";
        return s;
    };
    auto sorted = [&](ListSorting m) {
        auto v = base;
        sort_bookmarks(v, m);
        return names(v);
    };
    CHECK_EQ(sorted(ListSorting::AlphabeticalA), std::string("Alpha,Gamma,beta,"));  // case-sensitive, as Kotlin
    CHECK_EQ(sorted(ListSorting::AlphabeticalZ), std::string("beta,Gamma,Alpha,"));
    CHECK_EQ(sorted(ListSorting::UpdatedNew), std::string("beta,Gamma,Alpha,"));
    CHECK_EQ(sorted(ListSorting::UpdatedOld), std::string("Alpha,Gamma,beta,"));
    CHECK_EQ(sorted(ListSorting::ReleaseDateNew), std::string("Gamma,beta,Alpha,"));
    CHECK_EQ(sorted(ListSorting::ReleaseDateOld), std::string("Alpha,beta,Gamma,"));
    CHECK_EQ(static_cast<int>(ListSorting::AlphabeticalA), 5);  // CloudStream's ordinal (the stored value)
    CHECK_EQ(local_sortings().size(), size_t(6));
}

TEST("clearing continue watching keeps the saved positions (deleteAllResumeStateIds)") {
    Library lib;
    lib.set_view_pos_and_resume(show(), ep(1), ep(2), 600000, 1200000, 1);
    CHECK_EQ(lib.resume_list().size(), size_t(1));
    lib.clear_resume();
    CHECK(lib.resume_list().empty());
    CHECK(lib.view_pos("Prov", "data1").has_value());
    CHECK_EQ(lib.start_position_ms("Prov", "data1"), int64_t(600000));
}
