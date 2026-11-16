// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bundled SVG icons (assets/icons, white and "-dark" variants), addressed by name.
//
// IconView draws one. Icons do not go through the image service: its workers are shared with poster
// downloads, and at start-up (Home asking for its backdrop and every row's posters at once) a few-hundred-
// byte local icon waited seconds behind them, drawn as an empty square. An IconView rasterises its icon
// on the main thread the first time it is drawn at a size (a 24-unit SVG at button size takes well under
// a millisecond) and keeps the texture for every IconView of that icon and size.
#pragma once

#include "wnk/ui/view.hpp"

#include <string>

namespace csweb::ui {

void set_asset_root(const std::string &app_dir);
std::string icon_url(const std::string &name, bool dark = false);
// A bundled file under the app directory ("brand/logo.png").
std::string asset_url(const std::string &path);

// An icon (a file:// URL from icon_url), fitted inside the view and centred.
class IconView : public wnk::ui::View {
 public:
    explicit IconView(std::string url);
    const std::string &url() const { return url_; }

 protected:
    void draw(wnk::ui::Ui &ui) override;

 private:
    std::string url_;
};

}  // namespace csweb::ui
