// SPDX-License-Identifier: GPL-3.0-or-later
//
// What every screen may use, created once by the Application and alive until on_stop: the kit's
// context, the plugin runtime, the plugin manager, settings, and the router that opens screens.
#pragma once

#include "core/signal.hpp"
#include "library/library_store.hpp"
#include "plugins/plugin_data.hpp"
#include "plugins/plugin_manager.hpp"
#include "profiles/profiles.hpp"
#include "runtime/runtime_client.hpp"
#include "settings/settings_store.hpp"
#include "net/proxy_pool.hpp"
#include "torrent/torrent_service.hpp"

#include "wnk/app/app.hpp"
#include "wnk/media/device_caps.hpp"
#include "wnk/net/http.hpp"

namespace csweb::app {

class Router;

struct Services {
    wnk::app::Context *ctx = nullptr;
    runtime::RuntimeClient *runtime = nullptr;
    torrent::TorrentService *torrent = nullptr;
    net::ProxyPool *proxy = nullptr;  // the proxy for chosen providers' videos (Settings → Network)  // torrent and magnet links (a bundled TorrServer)
    plugins::PluginManager *plugins = nullptr;
    plugins::PluginDataStore *plugin_data = nullptr;
    settings::SettingsStore *settings = nullptr;
    library::LibraryStore *library = nullptr;
    Router *router = nullptr;
    wnk::net::HttpClient *http = nullptr;     // null without a network stack
    // The player's own transport: it keeps the cookies streaming servers set (Set-Cookie on a
    // playlist, checked on segments), as CloudStream's player (Cronet) does. Null: the app's.
    std::shared_ptr<wnk::net::HttpTransport> player_http;
    wnk::media::DeviceMediaCaps device;       // what the TV's decoders take (filled at start on the TV)
    core::Signal plugins_changed;  // providers, installed plugins or repositories changed
    core::Signal runtime_changed;  // the runtime started, stopped or failed
    profiles::Profiles profiles = profiles::Profiles::from_json(nlohmann::json());
    core::Signal profile_changed;  // another profile was chosen, or one was renamed
};

}  // namespace csweb::app
