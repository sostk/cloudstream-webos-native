// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/type_chips.hpp"

#include "settings/preferences.hpp"
#include "ui/widgets/pill_button.hpp"

#include <algorithm>

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr float kGap = 10;
}

TypeChips::TypeChips(int columns, Handler on_change)
    : Grid(columns, kGap), on_change_(std::move(on_change)), flow_(columns == 0) {
    set_focus_scope();
}

float TypeChips::flow(Ui &ui, float width, const wnk::Point *origin) {
    float x = 0, y = 0, line_h = 0;
    for (const auto &c : children()) {
        if (!c->visible()) continue;
        const wnk::Size s = c->measure(ui, {width, 1e6f});
        if (x > 0 && x + s.w > width) {  // wrap
            x = 0;
            y += line_h + kGap;
            line_h = 0;
        }
        if (origin != nullptr) c->layout(ui, place(ui, *c, {origin->x + x, origin->y + y, s.w, s.h}));
        x += s.w + kGap;
        line_h = std::max(line_h, s.h);
    }
    return y + line_h;
}

wnk::Size TypeChips::measure_content(Ui &ui, wnk::Size available) {
    if (!flow_) return Grid::measure_content(ui, available);
    return {available.w, flow(ui, available.w, nullptr)};
}

void TypeChips::layout_content(Ui &ui, const wnk::Rect &content) {
    if (!flow_) return Grid::layout_content(ui, content);
    const wnk::Point origin{content.x, content.y};
    flow(ui, content.w, &origin);
}

void TypeChips::set(const std::vector<runtime::Provider> &providers, const std::set<std::string> &selected) {
    selected_ = selected;
    clear_children();
    for (const auto &chip : settings::type_chips()) {
        if (!settings::chip_visible(chip, providers)) continue;
        const std::string label = chip.label;
        auto &b = add<PillButton>(label, "", PillButton::Style::Glass, [this, label](Ui &ui) {
            for (const auto &c : settings::type_chips()) {
                if (c.label != label) continue;
                selected_ = settings::toggle_chip(c, selected_);
                refresh();
                if (on_change_) on_change_(ui, selected_);
                return;
            }
        }, 52);
        b.name = "chip." + label;
    }
    refresh();
}

void TypeChips::refresh() {
    for (auto &child : children()) {
        auto *b = static_cast<PillButton *>(child.get());
        for (const auto &c : settings::type_chips()) {
            if (c.label == b->text()) {
                const bool on = settings::chip_selected(c, selected_);
                b->set_selected(on);
                b->set_icon(on ? "check" : "");
            }
        }
    }
}

}  // namespace csweb::ui
