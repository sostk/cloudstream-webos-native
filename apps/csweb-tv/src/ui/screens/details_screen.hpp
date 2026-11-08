// SPDX-License-Identifier: GPL-3.0-or-later
//
// Details (designs 03 and 04), behaving like CloudStream's ResultFragmentTv/ResultViewModel2:
//   - the provider's load() for the item: backdrop, title, meta line, tags, plot, cast, recommendations;
//   - a movie (or live stream) plays its own data; a series lists its episodes grouped by
//     (dub status, season) and, past 60 episodes, split into ranges of 50 (EPISODE_RANGE_SIZE on TV);
//   - Play starts the first episode of the group shown, or Resume the one DataStoreHelper kept;
//     choosing an episode opens the player; episodes show how much was watched;
//   - the bookmark button puts the show in one of the library lists (WatchType).
#pragma once

#include "app/router.hpp"
#include "runtime/episode_groups.hpp"
#include "runtime/runtime_types.hpp"

#include "wnk/ui/collection.hpp"
#include "wnk/ui/navigation.hpp"

#include <map>
#include <string>
#include <vector>

namespace csweb::ui {

class Backdrop;
class PillButton;

class DetailsScreen : public wnk::ui::Screen {
 public:
    DetailsScreen(app::Services &s, std::string provider, runtime::SearchResult item, bool resume = false);
    ~DetailsScreen() override;
    void on_show(wnk::ui::Ui &ui) override;


 private:
    void load(wnk::ui::Ui &ui);
    void show(wnk::ui::Ui &ui);
    void show_group(const runtime::EpisodeGroup &g, size_t range);
    void play(wnk::ui::Ui &ui, const runtime::Episode *ep);
    void play_resume(wnk::ui::Ui &ui);
    void refresh_library();
    void choose_list(wnk::ui::Ui &ui);
    library::Show show_info() const;
    static library::EpisodeRef episode_ref(const runtime::Episode &ep);
    std::string group_label(const runtime::EpisodeGroup &g) const;

    app::Services &s_;
    std::string provider_;
    runtime::SearchResult item_;
    runtime::Details details_;
    std::map<runtime::EpisodeGroup, std::vector<runtime::Episode>> groups_;
    runtime::EpisodeGroup group_;
    size_t range_ = 0;
    runtime::RuntimeClient::RequestId request_ = 0;
    bool loaded_ = false;
    bool resume_on_load_ = false;
    std::vector<std::pair<class EpisodeRow *, std::string>> rows_;  // shown rows and their episode data

    Backdrop *backdrop_ = nullptr;
    wnk::ui::Column *content_ = nullptr;
    wnk::ui::Label *title_ = nullptr, *meta_ = nullptr, *tags_ = nullptr, *plot_ = nullptr, *status_ = nullptr;
    PillButton *play_ = nullptr, *resume_ = nullptr, *bookmark_ = nullptr;
    PillButton *loading_ = nullptr;  // stands where Play will be until the details arrive (issue #26)
    core::Signal::Subscription library_sub_;
    core::Signal::Subscription plugins_sub_;  // waiting for the provider's plugin to load
    bool waiting_plugins_ = false;
    wnk::ui::Row *group_row_ = nullptr, *range_row_ = nullptr;
    wnk::ui::Column *episodes_ = nullptr;
    wnk::ui::Label *cast_title_ = nullptr, *more_title_ = nullptr;
    wnk::ui::CollectionView *cast_ = nullptr;  // ActorAdaptor: photo, name, role
    wnk::ui::CollectionView *more_ = nullptr;
    wnk::async::Lifetime life_;
};

}  // namespace csweb::ui
