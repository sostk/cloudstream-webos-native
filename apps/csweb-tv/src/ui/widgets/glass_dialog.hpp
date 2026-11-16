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

    // The width left for body content (the panel's width less its padding): wider content overflows.
    static constexpr float kContentWidth = 920 - 2 * 56;

 protected:
    void draw(wnk::ui::Ui &ui) override;

 private:
    glass::Surface surface_;
};

}  // namespace csweb::ui
