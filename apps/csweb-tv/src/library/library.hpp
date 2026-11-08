// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the viewer keeps, as CloudStream's DataStoreHelper keeps it:
//   - bookmarks: a show in one WatchType list (Watching, Completed, On hold, Dropped, Plan to watch);
//   - view positions: position and duration per episode or movie (setViewPos: only for videos of
//     30 s or more);
//   - resume: per show, the episode to continue (setViewPosAndResume: past 90 % the next episode
//     becomes the one to resume; past 90 % of the last one, the show leaves Continue watching).
// CloudStream keys these by integer ids derived from URLs; here a show is (provider, url) and an
// episode is (provider, the data loadLinks takes). Pure data: LibraryStore persists it.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace csweb::library {

enum class WatchType { Watching = 0, Completed = 1, OnHold = 2, Dropped = 3, PlanToWatch = 4, None = 5 };
const char *watch_type_label(WatchType t);

// LibraryViewModel's ListSorting, as numbered there (the stored library_sorting_mode is the ordinal);
// the local list supports the alphabetical, updated and release date orders.
enum class ListSorting { Query = 0, RatingHigh, RatingLow, UpdatedNew, UpdatedOld, AlphabeticalA, AlphabeticalZ,
                         ReleaseDateNew, ReleaseDateOld };
const std::vector<ListSorting> &local_sortings();
const char *sorting_label(ListSorting s);
// The lists in CloudStream's order (WatchType.entries without None).
const std::vector<WatchType> &watch_types();

struct Show {
    std::string provider, url, name, type, poster_url;
    int year = 0;
};

struct Bookmark {
    Show show;
    WatchType type = WatchType::None;
    int64_t bookmarked_ms = 0, updated_ms = 0;
};

struct Position {
    int64_t position_ms = 0, duration_ms = 0, updated_ms = 0;
};

struct EpisodeRef {
    std::string data;       // loadLinks data
    std::string name;       // "S1 · E3 Title" as shown
    int season = 0, episode = 0;
};

struct Resume {
    Show show;
    EpisodeRef episode;
    int64_t updated_ms = 0;
};

class Library {
 public:
    static std::string show_key(const std::string &provider, const std::string &url) { return provider + "\n" + url; }
    static std::string episode_key(const std::string &provider, const std::string &data) { return provider + "\n" + data; }

    // Bookmarks. Setting None removes the bookmark.
    void set_watch_type(const Show &show, WatchType type, int64_t now_ms);
    WatchType watch_type(const std::string &provider, const std::string &url) const;
    std::vector<Bookmark> bookmarks(WatchType type) const;  // most recently changed first

    // DataStoreHelper.setViewPosAndResume. `next` is the following episode, if any.
    void set_view_pos_and_resume(const Show &show, const EpisodeRef &current, const std::optional<EpisodeRef> &next,
                                 int64_t position_ms, int64_t duration_ms, int64_t now_ms);
    std::optional<Position> view_pos(const std::string &provider, const std::string &data) const;
    // GeneratorPlayer.getPos: where to start, 0 when unknown or past 95 %.
    int64_t start_position_ms(const std::string &provider, const std::string &data) const;
    std::optional<Resume> resume(const std::string &provider, const std::string &url) const;
    std::vector<Resume> resume_list() const;  // most recent first (getResumeWatching)
    void remove_resume(const std::string &provider, const std::string &url);
    void clear_resume();  // DataStoreHelper.deleteAllResumeStateIds: positions are kept

    nlohmann::json to_json() const;
    static Library from_json(const nlohmann::json &j);

    static constexpr size_t kMaxPositions = 2000;  // oldest dropped beyond this (the store is bounded)

 private:
    std::map<std::string, Bookmark> bookmarks_;
    std::map<std::string, Position> positions_;
    std::map<std::string, Resume> resume_;
};

// PosDur.fixVisual: the fraction a progress bar shows (≤1 % → 0, ≤5 % → 5 %, ≥95 % → full).
float visual_progress(const Position &p);

// SyncAPI.Page.sort for the local list: names compared as CloudStream does (case-sensitive), the
// release date being the show's year; ties keep the given order.
void sort_bookmarks(std::vector<Bookmark> &items, ListSorting method);

}  // namespace csweb::library
