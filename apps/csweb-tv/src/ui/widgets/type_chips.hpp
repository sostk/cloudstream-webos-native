// SPDX-License-Identifier: GPL-3.0-or-later
//
// CloudStream's TvType chips (tvtypes_chips: Movies, TV Series, Anime, …) as a grid of pills: only
// the chips some provider supports are shown, each toggles its types, and the handler gets every
// checked chip's types (HomeFragment.bindChips). With 0 columns the chips keep their own widths and
// wrap onto more lines when they do not fit, as CloudStream's ChipGroup does.
#pragma once

#include "runtime/runtime_types.hpp"

#include "wnk/ui/widgets.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace csweb::ui {

class TypeChips : public wnk::ui::Grid {
 public:
    using Handler = std::function<void(wnk::ui::Ui &, const std::set<std::string> &)>;
    TypeChips(int columns, Handler on_change);  // columns 0: a wrapping row
    // Rebuilds the chips for these providers with `selected` checked.
    void set(const std::vector<runtime::Provider> &providers, const std::set<std::string> &selected);
    const std::set<std::string> &selected() const { return selected_; }

 protected:
    wnk::Size measure_content(wnk::ui::Ui &ui, wnk::Size available) override;
    void layout_content(wnk::ui::Ui &ui, const wnk::Rect &content) override;

 private:
    // The flow layout: each chip at its own width, left to right, wrapping at `width`. Returns the
    // height used; with an `origin`, also lays the chips out from there.
    float flow(wnk::ui::Ui &ui, float width, const wnk::Point *origin);
    void refresh();
    Handler on_change_;
    std::set<std::string> selected_;
    bool flow_ = false;
};

}  // namespace csweb::ui
