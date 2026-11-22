// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kit delegate: builds the services in on_start (settings, plugin runtime, plugin manager,
// router), opens Home, shows plugin dialogs, and pauses the player when the app leaves the
// foreground. Where things live:
//   runtime   <app dir>/runtime-aarch64 on the TV (the package's private runtime), else
//             $CSWEB_RUNTIME_DIR or the repository's runtime/dist/runtime-<arch> (host runs);
//   plugins   <tmpfs>/plugins, written out from the store (DB8) at every start;
//   data      <tmpfs>/plugin-data, the plugins' SharedPreferences and files, copied to DB8 and back
//             (PluginDataStore: only /tmp is writable for a Dev Mode app besides DB8).
#pragma once

#include "app/router.hpp"
#include "app/services.hpp"
#include "core/stall_sampler.hpp"
#include "ui/dialogs/plugin_dialog_host.hpp"

#include "wnk/app/app.hpp"

#include <memory>

namespace csweb::app {

class Application : public wnk::app::Delegate {
 public:
    Application();
    ~Application() override;
    void on_start(wnk::app::Context &ctx) override;
    void on_lifecycle(wnk::app::Context &ctx, const wnk::app::Lifecycle::Transition &t) override;
    void on_relaunch(wnk::app::Context &ctx, const std::string &params_json) override;
    void on_frame(wnk::app::Context &ctx, float dt) override;
    void on_stop(wnk::app::Context &ctx) override;

 private:
    Services services_;
    std::unique_ptr<settings::SettingsStore> settings_;
    std::unique_ptr<library::LibraryStore> library_;
    std::unique_ptr<runtime::RuntimeClient> runtime_;
    std::unique_ptr<torrent::TorrentService> torrent_;
    std::unique_ptr<net::ProxyPool> proxy_;
    std::unique_ptr<plugins::PluginManager> plugins_;
    std::unique_ptr<plugins::PluginDataStore> plugin_data_;
    std::unique_ptr<wnk::net::HttpClient> http_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<ui::PluginDialogHost> dialogs_;
    std::unique_ptr<core::StallSampler> stalls_;
    void open_link(wnk::ui::Ui &ui, const std::string &params_json);
    void run_pending_search(wnk::ui::Ui &ui);
    void add_repository_from_link(wnk::ui::Ui &ui, const std::string &url);

    wnk::ui::Navigator *nav_ = nullptr;
    std::string pending_search_;  // a cloudstreamsearch:// query waiting for the plugins to load
    bool choosing_profile_ = false;  // the start-up profile picker is open
    std::string pending_repo_;    // a repository link waiting for the stored repositories to be read
    core::Signal::Subscription plugins_sub_;
    double since_data_save_s_ = 0;
    wnk::async::Lifetime life_;
};

}  // namespace csweb::app
