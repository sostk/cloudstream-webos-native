// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's dialogs: the kit's modal Dialog (title, message, body, buttons, focus trapped, Back
// dismisses) with its panel drawn as frosted glass (glass.hpp) over the artwork behind, as the nav bar
// and pills are, instead of the kit's flat surface colour.
#pragma once

#include "ui/widgets/glass.hpp"

#include "wnk/ui/navigation.hpp"

#include <string>

namespace csweb::ui {

class GlassDialog : public wnk::ui::Dialog {
 public:
    explicit GlassDialog(std::string title, std::string message = {});

    // The kit's panel: 920 wide with 56 padding each side. kContentWidth is what body content may use
    // (wider overflows the panel); set_panel_width widens a panel for content that needs more room.
    static constexpr float kPanelWidth = 920, kPadding = 56;
    static constexpr float kContentWidth = kPanelWidth - 2 * kPadding;
    static constexpr float content_width(float panel_width) { return panel_width - 2 * kPadding; }
    void set_panel_width(float width);

 protected:
    void draw(wnk::ui::Ui &ui) override;

 private:
    glass::Surface surface_;
};

}  // namespace csweb::ui
