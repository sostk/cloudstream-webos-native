// SPDX-License-Identifier: GPL-3.0-or-later
//
// CloudStream's "Clear history" on the Continue Watching row (HomeParentItemAdapterPreview): a
// confirmation, then DataStoreHelper.deleteAllResumeStateIds. Saved positions stay, as there.
#pragma once

#include "app/services.hpp"

#include "wnk/ui/ui.hpp"

#include <functional>

namespace csweb::ui {

// `cleared` runs after the delete, e.g. to move focus off a row that has just gone.
void confirm_clear_watch_history(wnk::ui::Ui &ui, app::Services &s,
                                 std::function<void(wnk::ui::Ui &)> cleared = {});

}  // namespace csweb::ui
