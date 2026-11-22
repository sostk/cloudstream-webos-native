// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings (design 15), CloudStream's settings that apply on a TV, in its sections:
//   Extensions                repositories and plugins
//   Profiles                  local profiles, the picker at start (AccountSelectActivity)
//   Providers                 provider languages, preferred media, test extensions (SettingsProviders)
//   Player                    preferred quality, autoplay next, seek times (SettingsPlayer)
//   Subtitles                 auto-select language, size, outline or box (SubtitlesFragment, as far
//                             as the kit's SubtitleView draws)
//   Layout                    cast on the details page (SettingsUI)
//   Search                    clear the search history
// and what is running (app version, plugin runtime state). Values live in SettingsStore under
// CloudStream's keys (settings/preferences.hpp).
#pragma once

#include "settings/proxy_rules.hpp"

#include "app/router.hpp"

#include "wnk/ui/navigation.hpp"

namespace wnk::ui {
class ValueRow;
}

namespace csweb::ui {

class SettingsScreen : public wnk::ui::Screen {
 public:
    explicit SettingsScreen(app::Services &s);
    void on_show(wnk::ui::Ui &ui) override;

 private:
    void refresh();
    void choose_languages(wnk::ui::Ui &ui);
    void choose_media(wnk::ui::Ui &ui);
    // Settings → Network → Proxies: the providers and their rules; one provider's rule; its own proxy.
    void open_proxies(wnk::ui::Ui &ui, const std::string &focus = {});
    void open_proxy_rule(wnk::ui::Ui &ui, const std::string &provider);
    void open_personal_proxy(wnk::ui::Ui &ui, const std::string &provider, const settings::ProxyRule &cur);
    app::Services &s_;
    wnk::ui::Label *runtime_ = nullptr, *home_ = nullptr;
    wnk::ui::ValueRow *langs_ = nullptr, *media_ = nullptr, *profiles_ = nullptr, *proxies_ = nullptr;
    wnk::ui::View *first_ = nullptr;
    bool shown_ = false;
    core::Signal::Subscription sub_;
};

}  // namespace csweb::ui
