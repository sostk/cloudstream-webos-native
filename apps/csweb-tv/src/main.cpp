// SPDX-License-Identifier: GPL-3.0-or-later
//
// CSWeb TV: a native webOS app that runs unchanged CloudStream plugins on the TV (apps/csweb-tv/README.md).
#include "app/application.hpp"
#include "ui/theme/theme.hpp"

#include <cstdlib>

// The legacy package (CSWEB_LEGACY, docs/legacy-build.md) installs beside the main one under its own id.
#ifdef CSWEB_LEGACY
#define CSWEB_FALLBACK_APP_ID "org.csweb.tv.legacy"
#define CSWEB_WINDOW_TITLE "CSWeb TV Legacy"
#else
#define CSWEB_FALLBACK_APP_ID "org.csweb.tv"
#define CSWEB_WINDOW_TITLE "CSWeb TV"
#endif

int main(int argc, char **argv) {
    wnk::app::AppConfig config;
    config.fallback_app_id = CSWEB_FALLBACK_APP_ID;
    config.title = CSWEB_WINDOW_TITLE;
    config.worker_threads = 4;
    // Symbols in provider names and row titles that the TV's fonts lack (CMakeLists.txt: Noto Emoji).
    config.fallback_fonts = {"emoji-font/NotoEmoji.ttf"};
    config.http = true;
    config.storage = true;
    config.theme = csweb::ui::theme::make_kit_theme();
    config.script_env_prefix = "WNK_CSWEB";
    // Scripted host runs (WNK_CSWEB_KEYS) step once per drawn frame: drawing every frame keeps them fast.
    if (std::getenv("WNK_CSWEB_KEYS")) config.redraw_on_change = false;
    return wnk::app::run(argc, argv, config, [] { return std::make_unique<csweb::app::Application>(); });
}
