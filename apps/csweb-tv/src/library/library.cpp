// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/library.hpp"

#include <algorithm>

namespace csweb::library {

using nlohmann::json;

namespace {
constexpr int64_t kMinDurationMs = 30000;    // setViewPos: "too short"
constexpr int64_t kNextEpisodePercent = 90;  // NEXT_WATCH_EPISODE_PERCENTAGE
constexpr int64_t kRestartPercent = 95;      // GeneratorPlayer.getPos

json show_json(const Show &s) {
    return {{"provider", s.provider}, {"url", s.url}, {"name", s.name}, {"type", s.type}, {"poster", s.poster_url},
            {"year", s.year}};
}
Show show_from(const json &j) {
    Show s;
    s.provider = j.value("provider", "");
    s.url = j.value("url", "");
    s.name = j.value("name", "");
    s.type = j.value("type", "");
    s.poster_url = j.value("poster", "");
    s.year = j.value("year", 0);
    return s;
}
json episode_json(const EpisodeRef &e) {
    return {{"data", e.data}, {"name", e.name}, {"season", e.season}, {"episode", e.episode}};
}
EpisodeRef episode_from(const json &j) {
    return {j.value("data", ""), j.value("name", ""), j.value("season", 0), j.value("episode", 0)};
}
}  // namespace

const char *watch_type_label(WatchType t) {
    switch (t) {
        case WatchType::Watching: return "Watching";
        case WatchType::Completed: return "Completed";
        case WatchType::OnHold: return "On hold";
        case WatchType::Dropped: return "Dropped";
        case WatchType::PlanToWatch: return "Plan to watch";
        case WatchType::None: return "None";
    }
    return "None";
}

const std::vector<WatchType> &watch_types() {
    static const std::vector<WatchType> all{WatchType::Watching, WatchType::Completed, WatchType::OnHold,
                                            WatchType::Dropped, WatchType::PlanToWatch};
    return all;
}

void Library::set_watch_type(const Show &show, WatchType type, int64_t now_ms) {
    const std::string key = show_key(show.provider, show.url);
    if (type == WatchType::None) {
        bookmarks_.erase(key);
        return;
    }
    auto it = bookmarks_.find(key);
    Bookmark b;
    b.show = show;
    b.type = type;
    b.bookmarked_ms = it != bookmarks_.end() ? it->second.bookmarked_ms : now_ms;
    b.updated_ms = now_ms;
    bookmarks_[key] = std::move(b);
}

WatchType Library::watch_type(const std::string &provider, const std::string &url) const {
    auto it = bookmarks_.find(show_key(provider, url));
    return it == bookmarks_.end() ? WatchType::None : it->second.type;
}

std::vector<Bookmark> Library::bookmarks(WatchType type) const {
    std::vector<Bookmark> out;
    for (const auto &kv : bookmarks_) {
        if (kv.second.type == type) out.push_back(kv.second);
    }
    std::stable_sort(out.begin(), out.end(), [](const Bookmark &a, const Bookmark &b) { return a.updated_ms > b.updated_ms; });
    return out;
}

void Library::set_view_pos_and_resume(const Show &show, const EpisodeRef &current, const std::optional<EpisodeRef> &next,
                                      int64_t position_ms, int64_t duration_ms, int64_t now_ms) {
    if (duration_ms <= 0) return;
    if (duration_ms >= kMinDurationMs) {
        positions_[episode_key(show.provider, current.data)] = {position_ms, duration_ms, now_ms};
        if (positions_.size() > kMaxPositions) {
            auto oldest = std::min_element(positions_.begin(), positions_.end(), [](const auto &a, const auto &b) {
                return a.second.updated_ms < b.second.updated_ms;
            });
            positions_.erase(oldest);
        }
    }
    const bool next_ep = position_ms * 100 / duration_ms >= kNextEpisodePercent;
    const std::string key = show_key(show.provider, show.url);
    if (next_ep && !next) {
        resume_.erase(key);  // watched the last one
        return;
    }
    resume_[key] = {show, next_ep ? *next : current, now_ms};
}

std::optional<Position> Library::view_pos(const std::string &provider, const std::string &data) const {
    auto it = positions_.find(episode_key(provider, data));
    if (it == positions_.end()) return std::nullopt;
    return it->second;
}

int64_t Library::start_position_ms(const std::string &provider, const std::string &data) const {
    const auto p = view_pos(provider, data);
    if (!p || p->duration_ms == 0) return 0;
    if (p->position_ms * 100 / p->duration_ms > kRestartPercent) return 0;
    return p->position_ms;
}

std::optional<Resume> Library::resume(const std::string &provider, const std::string &url) const {
    auto it = resume_.find(show_key(provider, url));
    if (it == resume_.end()) return std::nullopt;
    return it->second;
}

std::vector<Resume> Library::resume_list() const {
    std::vector<Resume> out;
    for (const auto &kv : resume_) out.push_back(kv.second);
    std::stable_sort(out.begin(), out.end(), [](const Resume &a, const Resume &b) { return a.updated_ms > b.updated_ms; });
    return out;
}

void Library::remove_resume(const std::string &provider, const std::string &url) { resume_.erase(show_key(provider, url)); }

void Library::clear_resume() { resume_.clear(); }

json Library::to_json() const {
    json b = json::array(), p = json::array(), r = json::array();
    for (const auto &kv : bookmarks_) {
        const auto &x = kv.second;
        b.push_back({{"show", show_json(x.show)}, {"type", static_cast<int>(x.type)}, {"bookmarked", x.bookmarked_ms},
                     {"updated", x.updated_ms}});
    }
    for (const auto &kv : positions_) {
        p.push_back({{"key", kv.first}, {"pos", kv.second.position_ms}, {"dur", kv.second.duration_ms},
                     {"updated", kv.second.updated_ms}});
    }
    for (const auto &kv : resume_) {
        r.push_back({{"show", show_json(kv.second.show)}, {"episode", episode_json(kv.second.episode)},
                     {"updated", kv.second.updated_ms}});
    }
    return {{"version", 1}, {"bookmarks", b}, {"positions", p}, {"resume", r}};
}

Library Library::from_json(const json &j) {
    Library lib;
    if (!j.is_object()) return lib;
    auto array = [&j](const char *k) {
        auto it = j.find(k);
        return it != j.end() && it->is_array() ? *it : json::array();
    };
    for (const auto &x : array("bookmarks")) {
        if (!x.is_object() || !x.contains("show")) continue;
        Bookmark b;
        b.show = show_from(x["show"]);
        const int t = x.value("type", 5);
        if (t < 0 || t > 4) continue;
        b.type = static_cast<WatchType>(t);
        b.bookmarked_ms = x.value("bookmarked", int64_t(0));
        b.updated_ms = x.value("updated", int64_t(0));
        lib.bookmarks_[show_key(b.show.provider, b.show.url)] = std::move(b);
    }
    for (const auto &x : array("positions")) {
        if (!x.is_object()) continue;
        lib.positions_[x.value("key", "")] = {x.value("pos", int64_t(0)), x.value("dur", int64_t(0)), x.value("updated", int64_t(0))};
    }
    for (const auto &x : array("resume")) {
        if (!x.is_object() || !x.contains("show") || !x.contains("episode")) continue;
        Resume r{show_from(x["show"]), episode_from(x["episode"]), x.value("updated", int64_t(0))};
        lib.resume_[show_key(r.show.provider, r.show.url)] = std::move(r);
    }
    return lib;
}

float visual_progress(const Position &p) {
    if (p.duration_ms <= 0) return 0;
    const int64_t pct = p.position_ms * 100 / p.duration_ms;
    if (pct <= 1) return 0;
    if (pct <= 5) return 0.05f;
    if (pct >= 95) return 1;
    return static_cast<float>(p.position_ms) / static_cast<float>(p.duration_ms);
}

const std::vector<ListSorting> &local_sortings() {
    static const std::vector<ListSorting> v = {ListSorting::AlphabeticalA, ListSorting::AlphabeticalZ, ListSorting::UpdatedNew,
                                               ListSorting::UpdatedOld,    ListSorting::ReleaseDateNew, ListSorting::ReleaseDateOld};
    return v;
}

const char *sorting_label(ListSorting s) {
    switch (s) {
        case ListSorting::Query: return "None";
        case ListSorting::RatingHigh: return "Rating (high to low)";
        case ListSorting::RatingLow: return "Rating (low to high)";
        case ListSorting::UpdatedNew: return "Updated (new to old)";
        case ListSorting::UpdatedOld: return "Updated (old to new)";
        case ListSorting::AlphabeticalA: return "Alphabetical (A-Z)";
        case ListSorting::AlphabeticalZ: return "Alphabetical (Z-A)";
        case ListSorting::ReleaseDateNew: return "Release date (new to old)";
        case ListSorting::ReleaseDateOld: return "Release date (old to new)";
    }
    return "";
}

void sort_bookmarks(std::vector<Bookmark> &items, ListSorting method) {
    auto by = [&items](auto key) {
        std::stable_sort(items.begin(), items.end(), [&key](const Bookmark &a, const Bookmark &b) { return key(a) < key(b); });
    };
    switch (method) {
        case ListSorting::AlphabeticalA: by([](const Bookmark &b) { return b.show.name; }); break;
        case ListSorting::AlphabeticalZ:  // sortedBy { name }.reversed()
            by([](const Bookmark &b) { return b.show.name; });
            std::reverse(items.begin(), items.end());
            break;
        case ListSorting::UpdatedNew: by([](const Bookmark &b) { return -b.updated_ms; }); break;
        case ListSorting::UpdatedOld: by([](const Bookmark &b) { return b.updated_ms; }); break;
        case ListSorting::ReleaseDateNew:  // sortedByDescending
            std::stable_sort(items.begin(), items.end(), [](const Bookmark &a, const Bookmark &b) { return a.show.year > b.show.year; });
            break;
        case ListSorting::ReleaseDateOld: by([](const Bookmark &b) { return b.show.year; }); break;
        default: break;
    }
}

}  // namespace csweb::library
