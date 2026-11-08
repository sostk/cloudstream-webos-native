// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/clear_watch_history.hpp"

#include "library/library_store.hpp"

#include "wnk/log.hpp"
#include "wnk/ui/navigation.hpp"

#include <memory>

namespace csweb::ui {

using namespace wnk::ui;

void confirm_clear_watch_history(Ui &ui, app::Services &s, std::function<void(Ui &)> cleared) {
    // R.string.clear_history / delete_message("Continue Watching") / cancel / delete.
    auto d = std::make_unique<Dialog>("Clear history", "This will permanently delete Continue Watching\nAre you sure?");
    d->add_button("Cancel");
    app::Services *services = &s;
    d->add_button("Delete", [services, cleared](Ui &u) {
        services->library->update([](library::Library &lib) { lib.clear_resume(); });
        WNK_LOGI("csweb", "library: continue watching cleared");
        u.toast("Watch history cleared.");
        if (cleared) cleared(u);
    }).name = "watch_history.delete";
    ui.show_dialog(std::move(d));
}

}  // namespace csweb::ui
