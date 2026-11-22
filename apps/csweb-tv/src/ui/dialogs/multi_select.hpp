// SPDX-License-Identifier: GPL-3.0-or-later
//
// CloudStream's showMultiDialog: a list of check rows and Apply; the handler gets the checked
// indices. Cancel or Back leaves the setting as it was.
#pragma once

#include "wnk/ui/ui.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace csweb::ui {

void show_multi_select(wnk::ui::Ui &ui, const std::string &title, const std::vector<std::string> &options,
                       const std::set<size_t> &checked,
                       std::function<void(wnk::ui::Ui &, const std::set<size_t> &)> on_apply);

// One choice from a scrolling list (the current one focused); picking it closes the dialog.
void show_single_select(wnk::ui::Ui &ui, const std::string &title, const std::vector<std::string> &options,
                        size_t selected, std::function<void(wnk::ui::Ui &, size_t)> on_pick);

}  // namespace csweb::ui
