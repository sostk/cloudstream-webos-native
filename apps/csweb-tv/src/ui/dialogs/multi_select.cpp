// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/multi_select.hpp"

#include "ui/widgets/glass_dialog.hpp"

#include "wnk/ui/forms.hpp"
#include "wnk/ui/navigation.hpp"

#include <memory>

namespace csweb::ui {

using namespace wnk::ui;

void show_multi_select(Ui &ui, const std::string &title, const std::vector<std::string> &options,
                       const std::set<size_t> &checked, std::function<void(Ui &, const std::set<size_t> &)> on_apply) {
    auto d = std::make_unique<GlassDialog>(title);
    auto state = std::make_shared<std::set<size_t>>(checked);
    auto &scroll = d->body().add<ScrollView>();
    scroll.set_height(Dim::fixed(options.size() > 7 ? 560.0f : options.size() * 80.0f));
    scroll.set_width(Dim::fixed(GlassDialog::kContentWidth));
    auto &list = scroll.add<Column>(6);
    View *first = nullptr;
    for (size_t i = 0; i < options.size(); i++) {
        auto &t = list.add<Toggle>(options[i], checked.count(i) != 0, [state, i](Ui &, bool on) {
            if (on) state->insert(i);
            else state->erase(i);
        });
        t.set_style(Toggle::Style::Check);
        t.name = "multi." + std::to_string(i);
        if (first == nullptr) first = &t;
    }
    // Dialog buttons dismiss the dialog before their handler runs.
    d->add_button("Apply", [state, on_apply](Ui &u) {
        if (on_apply) on_apply(u, *state);
    });
    d->add_button("Cancel");
    ui.show_dialog(std::move(d));
    if (first != nullptr) ui.focus(first);
}

}  // namespace csweb::ui
