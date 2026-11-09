// SPDX-License-Identifier: GPL-3.0-or-later
//
// Home (design 01), behaving like CloudStream's HomeFragment/HomeViewModel:
//   - one provider at a time, the one the viewer chose ("home_api_used"); none until one is chosen;
//     Random picks one and stores it;
//   - every main page section of that provider is requested (APIRepository.getMainPage): all at once,
//     or one after another with the provider's delay when it sets sequentialMainPage;
//   - each section is a row; the hero shows an item picked from the shuffled, de-duplicated items,
//     with its full details loaded (CloudStream's preview: backdrop, plot);
//   - no provider or no plugins: an empty state that leads to Extensions;
//   - above the provider's rows, Continue watching (HomeViewModel.getResumeWatching): the shows with
//     an episode to resume, most recent first; choosing one opens it and resumes.
#pragma once

#include "app/router.hpp"
#include "runtime/runtime_types.hpp"

#include "wnk/ui/collection.hpp"
#include "wnk/ui/navigation.hpp"

#include <deque>
#include <random>
#include <string>
#include <vector>

namespace csweb::ui {

class Backdrop;
class PillButton;
class TopBar;

class HomeScreen : public wnk::ui::Screen {
 public:
    explicit HomeScreen(app::Services &s);
    ~HomeScreen() override;
    void on_show(wnk::ui::Ui &ui) override;

 protected:
    void update(wnk::ui::Ui &ui, float dt) override;

 private:
    struct Row {
        std::string name;
        bool horizontal = false;
        std::vector<runtime::SearchResult> items;
        wnk::ui::Label *title = nullptr;
        wnk::ui::CollectionView *rail = nullptr;
    };
    struct PendingSection {
        int index;
        double not_before_ms;
    };

    void refresh(wnk::ui::Ui &ui);           // providers changed: (re)load if the shown provider changed
    std::string chosen_provider() const;
    std::string random_provider() const;
    void load(wnk::ui::Ui &ui, const std::string &provider);
    void request_section(wnk::ui::Ui &ui, int index);
    Row &row_for(const std::string &name, bool horizontal);
    void rebuild_row(Row &row);
    void pick_hero();
    void show_hero(const runtime::Details &d);
    void open_provider_picker(wnk::ui::Ui &ui);
    enum class StateAction { None, Extensions, ChooseSource };
    void set_state_text(const std::string &text, StateAction action);
    void cancel_requests();
    void rebuild_resume();

    app::Services &s_;
    std::string provider_;  // shown provider name ("" when none)
    std::vector<std::unique_ptr<Row>> rows_;
    std::deque<PendingSection> pending_;
    std::vector<runtime::RuntimeClient::RequestId> requests_;
    std::vector<std::string> hero_seen_;
    bool hero_set_ = false;
    mutable std::mt19937 rng_{std::random_device{}()};

    Backdrop *backdrop_ = nullptr;
    TopBar *top_ = nullptr;
    bool shown_ = false;
    double requested_ms_ = 0;      // when the current source's rows were asked for
    bool first_row_logged_ = false;
    PillButton *provider_button_ = nullptr;
    wnk::ui::Label *clock_ = nullptr;
    wnk::ui::Column *content_ = nullptr;
    wnk::ui::Column *hero_ = nullptr;
    wnk::ui::Label *hero_kicker_ = nullptr, *hero_title_ = nullptr, *hero_meta_ = nullptr, *hero_plot_ = nullptr;
    PillButton *hero_play_ = nullptr, *hero_info_ = nullptr;
    runtime::SearchResult hero_item_;
    wnk::ui::Column *state_ = nullptr;
    wnk::ui::Label *state_text_ = nullptr;
    PillButton *state_button_ = nullptr;
    StateAction state_action_ = StateAction::None;
    wnk::ui::Column *rows_box_ = nullptr;
    wnk::ui::Row *resume_title_ = nullptr;  // the title and CloudStream's Clear history button
    wnk::ui::CollectionView *resume_rail_ = nullptr;
    std::vector<library::Resume> resume_;
    core::Signal::Subscription library_sub_;
    core::Signal::Subscription profile_sub_;  // another profile: its home source
    core::Signal::Subscription plugins_sub_;
    double last_clock_ms_ = -1e9;
    wnk::async::Lifetime life_;  // destroyed first: late runtime answers find the screen gone
};

}  // namespace csweb::ui
