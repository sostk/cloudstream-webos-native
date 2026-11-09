// SPDX-License-Identifier: GPL-3.0-or-later
//
// Repositories and plugins, as CloudStream's RepositoryManager + PluginManager handle them, on the
// webOS app's terms:
//   - add a repository from what the user typed (URL, cloudstreamrepo://, shortcode), read its
//     repo.json and plugin lists;
//   - install a plugin: download (size-limited), check the list's fileHash when it has one, check the
//     archive, record its SHA-256, store it (DB8), write it out and load it in the runtime;
//   - after every runtime start, write out and load every enabled plugin (each load passes the
//     recorded hash; the runtime refuses a file that changed);
//   - keep the providers the loaded plugins registered, for every screen.
//
// Main thread only. Network and store work runs on the pool; results come back through the
// dispatcher and are dropped if the manager is gone (Lifetime).
#pragma once

#include "plugins/load_hints.hpp"
#include "plugins/plugin_store.hpp"
#include "plugins/repository.hpp"
#include "runtime/runtime_client.hpp"
#include "runtime/runtime_types.hpp"

#include "wnk/async/async.hpp"
#include "wnk/net/http.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace csweb::plugins {

class PluginManager {
 public:
    using Done = std::function<void(bool ok, const std::string &error)>;

    struct Deps {
        std::shared_ptr<wnk::net::HttpTransport> http;  // may be null (no network stack)
        wnk::async::WorkerPool *pool = nullptr;
        wnk::async::MainDispatcher *main = nullptr;
        wnk::storage::KeyValueStore *kv = nullptr;      // may be null (store unavailable)
        runtime::RuntimeClient *runtime = nullptr;
        std::string plugin_dir;                         // tmpfs directory the runtime reads plugins from
        std::function<std::string()> home_provider;     // the Home source; its plugin loads first
    };

    explicit PluginManager(Deps deps);
    ~PluginManager();

    // Reads repositories and installed plugins; plugins are loaded once the runtime is ready.
    void start();
    // Called by the owner on every RuntimeClient::on_ready (first start and restarts).
    void on_runtime_ready();

    const std::vector<RepositoryData> &repositories() const { return repos_; }
    const std::vector<InstalledPlugin> &installed() const { return installed_; }
    const std::vector<runtime::Provider> &providers() const { return providers_; }
    const runtime::Provider *provider(const std::string &name) const;
    // Plugin load results: id -> error (empty when loaded).
    const std::map<std::string, std::string> &load_errors() const { return load_errors_; }
    // True until every enabled plugin has been loaded or has failed (also while the runtime starts).
    bool loading() const;
    // The stored repositories and plugins have been read (start() is asynchronous).
    bool started() const { return started_; }

    void add_repository(const std::string &typed, Done done);
    void remove_repository(const std::string &url, Done done);
    using PluginsDone = std::function<void(bool ok, const std::string &error, std::vector<SitePlugin> plugins)>;
    void fetch_plugins(const RepositoryData &repo, PluginsDone done);

    void install(const RepositoryData &repo, const SitePlugin &plugin, Done done);
    void uninstall(const std::string &id, Done done);
    void set_enabled(const std::string &id, bool enabled, Done done);
    // Unloads and loads an installed plugin again, so changed plugin settings take effect.
    void reload(const std::string &id);
    // By the repository the user added (CloudStream keys plugins by that URL and the internal name,
    // not by the list's own repositoryUrl field, which names the source code).
    const InstalledPlugin *installed_plugin(const std::string &repository_url, const std::string &internal_name) const;

    // Where an installed plugin is written out for the runtime (what `load` and `settings` name).
    std::string plugin_path(const std::string &id) const { return deps_.plugin_dir + "/" + id + ".cs3"; }

    std::function<void()> on_changed;  // repositories, installed plugins, providers or loading changed

 private:
    void fetch(const std::string &url, size_t max_bytes, int max_redirects,
               std::function<void(wnk::net::HttpResponse)> done);
    void load_plugin(const InstalledPlugin &p);
    void send_load(const InstalledPlugin &p, const std::string &path, const std::string &error);
    void load_in_order(std::shared_ptr<std::vector<InstalledPlugin>> list, size_t index);
    void load_all();
    void save_hints();
    void unload_plugin(const std::string &id, std::function<void()> then);
    void changed();

    Deps deps_;
    std::unique_ptr<PluginStore> store_;
    std::unique_ptr<wnk::net::HttpClient> http_;
    std::vector<RepositoryData> repos_;
    std::vector<InstalledPlugin> installed_;
    std::vector<runtime::Provider> providers_;
    std::map<std::string, std::string> load_errors_;
    int pending_loads_ = 0;
    LoadHints hints_;
    bool hints_dirty_ = false;
    bool started_ = false;
    bool runtime_ready_ = false;
    wnk::async::Lifetime life_;
};

}  // namespace csweb::plugins
