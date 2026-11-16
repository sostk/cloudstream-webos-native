// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/glass_dialog.hpp"

namespace csweb::ui {

GlassDialog::GlassDialog(std::string title, std::string message) : Dialog(std::move(title), std::move(message)) {}

void GlassDialog::draw(wnk::ui::Ui &ui) {
    // As wnk::ui::Dialog::draw: the scrim and the panel fade in together. The kit's Dialog animates its
    // panel (its first child) by opacity and translation; the glass follows both.
    if (children().empty()) return;
    const wnk::ui::View &panel = *children().front();
    const float t = panel.opacity();
    const wnk::ui::Theme &th = ui.theme();
    ui.renderer().fill_rect(frame(), ui.fade(th.scrim.with_alpha(th.scrim.a * t)));
    surface_.draw(ui, panel.frame(), 24, glass::dialog_style(), t, panel.translation());
}

}  // namespace csweb::ui
