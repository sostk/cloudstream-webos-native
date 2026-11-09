// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/plugin_manager.hpp"

#include "core/http_url.hpp"
#include "core/sha256.hpp"
#include "core/zip_check.hpp"

#include "wnk/log.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>

namespace csweb::plugins {

namespace {

constexpr const char *kCat = "csweb";
constexpr size_t kMaxRepoBytes = 1u << 20;     // repo.json, plugins.json
constexpr size_t kMaxPluginBytes = 32u << 20;  // a .cs3

struct StoreRead {
    std::vector<RepositoryData> repos;
    std::vector<InstalledPlugin> installed;
    std::string hints;
};

struct InstallResult {
    bool ok = false;
    std::string error;
    InstalledPlugin record;
};

std::string net_error(const wnk::net::HttpResponse &r) {
    if (r.error != wnk::net::NetError::None) return std::string("network: ") + wnk::net::error_name(r.error);
    return "HTTP " + std::to_string(r.status);
}

std::string file_id(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind(".cs3");
    return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace

PluginManager::PluginManager(Deps deps) : deps_(std::move(deps)) {
    if (deps_.kv) store_ = std::make_unique<PluginStore>(*deps_.kv);
    if (deps_.http) http_ = std::make_unique<wnk::net::HttpClient>(deps_.http, *deps_.pool, *deps_.main);
}

PluginManager::~PluginManager() = default;

void PluginManager::changed() {
    if (on_changed) on_changed();
}

const runtime::Provider *PluginManager::provider(const std::string &name) const {
    for (const auto &p : providers_) if (p.name == name) return &p;
    return nullptr;
}

const InstalledPlugin *PluginManager::installed_plugin(const std::string &repository_url, const std::string &internal_name) const {
    const std::string id = plugin_id(repository_url, internal_name);
    for (const auto &p : installed_) if (p.id == id) return &p;
    return nullptr;
}

void PluginManager::start() {
    if (!store_) {
        WNK_LOGW(kCat, "plugins: no key/value store; repositories and plugins cannot be kept");
        started_ = true;
        changed();
        return;
    }
    PluginStore *store = store_.get();
    wnk::async::run_then_post<StoreRead>(
        *deps_.pool, *deps_.main, life_.weak(), {},
        [store](const wnk::async::CancellationToken &) {
            return StoreRead{store->repositories(), store->installed(), store->load_hints()};
        },
        [this](StoreRead r) {
            repos_ = std::move(r.repos);
            installed_ = std::move(r.installed);
            hints_ = parse_load_hints(r.hints);
            started_ = true;
            WNK_LOGI(kCat, "plugins: %zu repositories, %zu installed", repos_.size(), installed_.size());
            if (runtime_ready_) load_all();
            changed();
        });
}

void PluginManager::on_runtime_ready() {
    runtime_ready_ = true;
    providers_.clear();
    load_errors_.clear();
    pending_loads_ = 0;
    if (started_) load_all();
    changed();
}

void PluginManager::load_all() {
    // The runtime loads one plugin at a time, in the order asked: the Home source's plugin first lets
    // Home request its rows as soon as that one is ready.
    const std::string home = deps_.home_provider ? deps_.home_provider() : std::string();
    const auto order = load_order(installed_, hints_, home);
    if (!order.empty() && order.front() != 0 && hint_for(hints_, installed_[order.front()])) {
        WNK_LOGI(kCat, "plugins: loading %s first (Home source)", installed_[order.front()].internal_name.c_str());
    }
    // Each is written out, then its request sent, then the next is written: the runtime loads in the
    // order requests arrive, and tasks of their own could finish writing in any order.
    std::vector<InstalledPlugin> list;
    for (size_t i : order) {
        if (installed_[i].enabled) list.push_back(installed_[i]);
    }
    if (list.empty() || !store_ || !deps_.runtime) return;
    pending_loads_ += static_cast<int>(list.size());
    load_in_order(std::make_shared<std::vector<InstalledPlugin>>(std::move(list)), 0);
}

void PluginManager::load_in_order(std::shared_ptr<std::vector<InstalledPlugin>> list, size_t index) {
    if (index >= list->size()) return;
    PluginStore *store = store_.get();
    const std::string dir = deps_.plugin_dir;
    const InstalledPlugin p = (*list)[index];
    using Written = std::pair<std::string, std::string>;  // path, error
    wnk::async::run_then_post<Written>(
        *deps_.pool, *deps_.main, life_.weak(), {},
        [store, p, dir](const wnk::async::CancellationToken &) {
            std::string error;
            std::string path = store->materialize(p, dir, &error);
            return Written{std::move(path), std::move(error)};
        },
        [this, list, index, p](Written w) {
            send_load(p, w.first, w.second);
            load_in_order(list, index + 1);
        });
}

void PluginManager::save_hints() {
    if (!store_ || !hints_dirty_ || pending_loads_ > 0) return;
    hints_dirty_ = false;
    for (auto it = hints_.begin(); it != hints_.end();) {  // uninstalled plugins
        const bool installed = std::any_of(installed_.begin(), installed_.end(), [&](const InstalledPlugin &p) { return p.id == it->first; });
        it = installed ? std::next(it) : hints_.erase(it);
    }
    PluginStore *store = store_.get();
    const std::string text = dump_load_hints(hints_);
    wnk::async::run_then_post<bool>(
        *deps_.pool, *deps_.main, life_.weak(), {},
        [store, text](const wnk::async::CancellationToken &) {
            std::string error;
            const bool ok = store->save_load_hints(text, &error);
            if (!ok) WNK_LOGW(kCat, "plugins: load hints not saved: %s", error.c_str());
            return ok;
        },
        [](bool) {});
}

void PluginManager::load_plugin(const InstalledPlugin &p) {
    if (!store_ || !deps_.runtime) return;
    pending_loads_++;
    PluginStore *store = store_.get();
    const std::string dir = deps_.plugin_dir;
    struct Out {
        std::string path, error;
    };
    wnk::async::run_then_post<Out>(
        *deps_.pool, *deps_.main, life_.weak(), {},
        [store, p, dir](const wnk::async::CancellationToken &) {
            Out o;
            o.path = store->materialize(p, dir, &o.error);
            return o;
        },
        [this, p](Out o) { send_load(p, o.path, o.error); });
}

void PluginManager::send_load(const InstalledPlugin &p, const std::string &path, const std::string &error) {
    if (path.empty()) {
        pending_loads_--;
        load_errors_[p.id] = error;
        WNK_LOGW(kCat, "plugins: %s not written: %s", p.internal_name.c_str(), error.c_str());
        changed();
        return;
    }
    nlohmann::json req = {{"op", "load"}, {"plugin", path}, {"sha256", p.sha256}};
    const LoadHint *known = hint_for(hints_, p);
    if (known && !known->activity.empty()) req["activity"] = known->activity;
    auto learned = std::make_shared<LoadHint>();
    auto stages = std::make_shared<std::string>();  // the runtime's per-stage times, for the log
    learned->sha256 = p.sha256;
    const auto started = std::chrono::steady_clock::now();
    deps_.runtime->request(
        req,
        [this, learned, stages](const nlohmann::json &e) {
            const std::string ev = e.value("event", "");
            if (ev == "timing") {
                for (const auto &[stage, v] : e.items()) {
                    if (stage == "event" || stage == "id" || !v.is_number_integer()) continue;
                    *stages += (stages->empty() ? "" : " ") + stage + " " + std::to_string(v.get<long long>());
                }
            }
            if (ev == "provider") {
                providers_.push_back(runtime::parse_provider(e));
                learned->providers.push_back(providers_.back().name);
            } else if (ev == "compat" && e.value("context", "") == "headless-activity") {
                const std::string type = e.value("type", "");
                if (valid_activity_name(type)) learned->activity = type;
            }
        },
        [this, p, learned, stages, started](bool ok, const std::string &error) {
            pending_loads_--;
            load_errors_[p.id] = ok ? std::string() : error;
            const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started).count();
            if (!ok) {
                WNK_LOGW(kCat, "plugins: %s did not load: %s", p.internal_name.c_str(), error.c_str());
            } else {
                WNK_LOGI(kCat, "plugins: %s loaded in %lld ms, %zu providers%s%s%s%s%s", p.internal_name.c_str(), ms,
                         learned->providers.size(), learned->activity.empty() ? "" : ", with ",
                         learned->activity.c_str(), stages->empty() ? "" : " (runtime ms: ", stages->c_str(),
                         stages->empty() ? "" : ")");
                const LoadHint *before = hint_for(hints_, p);
                if (!before || before->providers != learned->providers || before->activity != learned->activity) {
                    hints_[p.id] = *learned;
                    hints_dirty_ = true;
                }
            }
            save_hints();
            changed();
        });
}

bool PluginManager::loading() const {
    if (!started_ || pending_loads_ > 0) return true;
    if (runtime_ready_) return false;
    // Plugins wait for the runtime, unless it has failed for good.
    if (deps_.runtime && deps_.runtime->state() == runtime::RuntimeClient::State::Failed) return false;
    return std::any_of(installed_.begin(), installed_.end(), [](const InstalledPlugin &p) { return p.enabled; });
}

void PluginManager::reload(const std::string &id) {
    for (const auto &p : installed_) {
        if (p.id != id || !p.enabled) continue;
        const InstalledPlugin copy = p;
        unload_plugin(id, [this, copy] { load_plugin(copy); });
        return;
    }
}

void PluginManager::unload_plugin(const std::string &id, std::function<void()> then) {
    const std::string path = plugin_path(id);
    auto drop = [this, id, path, then] {
        providers_.erase(std::remove_if(providers_.begin(), providers_.end(),
                                        [&](const runtime::Provider &p) { return file_id(p.plugin) == id; }),
                         providers_.end());
        load_errors_.erase(id);
        if (then) then();
    };
    const bool loaded = std::any_of(providers_.begin(), providers_.end(),
                                    [&](const runtime::Provider &p) { return file_id(p.plugin) == id; }) ||
                        (load_errors_.count(id) && load_errors_[id].empty());
    if (!loaded || !deps_.runtime || deps_.runtime->state() != runtime::RuntimeClient::State::Ready) {
        drop();
        return;
    }
    deps_.runtime->request({{"op", "unload"}, {"plugin", path}}, {}, [drop](bool, const std::string &) { drop(); });
}

void PluginManager::fetch(const std::string &url, size_t max_bytes, int max_redirects,
                          std::function<void(wnk::net::HttpResponse)> done) {
    if (!http_) {
        wnk::net::HttpResponse r;
        r.error = wnk::net::NetError::Unsupported;
        done(std::move(r));
        return;
    }
    wnk::net::HttpRequest req;
    req.url = core::canonical_http_url(url);  // as OkHttp sends it: "My Plugin.cs3" -> "My%20Plugin.cs3"
    req.max_response_bytes = max_bytes;
    req.max_redirects = max_redirects;
    req.timeout_ms = 60000;
    // send() does not call back for a request it refuses (a URL it cannot use); report that here so
    // an install or a repository read never waits for ever.
    auto shared = std::make_shared<std::function<void(wnk::net::HttpResponse)>>(std::move(done));
    if (!http_->send(req, {}, life_.weak(), [shared](wnk::net::HttpResponse r) { (*shared)(std::move(r)); })) {
        wnk::net::HttpResponse r;
        r.error = wnk::net::NetError::InvalidRequest;
        (*shared)(std::move(r));
    }
}

void PluginManager::add_repository(const std::string &typed, Done done) {
    const RepoInput in = parse_repo_input(typed);
    if (in.kind == RepoInput::Kind::Invalid) {
        done(false, "Not a repository URL or shortcode");
        return;
    }
    auto read_repo = [this, done](const std::string &url) {
        for (const auto &r : repos_) {
            if (r.url == url) {
                done(false, "This repository is already added");
                return;
            }
        }
        fetch(url, kMaxRepoBytes, 5, [this, url, done](wnk::net::HttpResponse res) {
            if (!res.ok()) {
                done(false, "Could not read the repository (" + net_error(res) + ")");
                return;
            }
            std::string error;
            auto repo = parse_repository(res.body, &error);
            if (!repo) {
                done(false, error);
                return;
            }
            auto repos = repos_;
            repos.push_back({repo->name, url, repo->icon_url});
            PluginStore *store = store_.get();
            if (!store) {
                done(false, "Storage is not available on this TV");
                return;
            }
            wnk::async::run_then_post<std::string>(
                *deps_.pool, *deps_.main, life_.weak(), {},
                [store, repos](const wnk::async::CancellationToken &) {
                    std::string e;
                    return store->save_repositories(repos, &e) ? std::string() : (e.empty() ? "store" : e);
                },
                [this, repos, done](std::string e) {
                    if (!e.empty()) {
                        done(false, "Could not save the repository (" + e + ")");
                        return;
                    }
                    repos_ = repos;
                    changed();
                    done(true, {});
                });
        });
    };
    if (in.kind == RepoInput::Kind::Url) {
        read_repo(in.url);
        return;
    }
    // A shortcode: the service answers with a redirect whose Location is the repository.
    fetch(in.url, 64u << 10, 0, [in, read_repo, done](wnk::net::HttpResponse res) {
        const std::string location = accept_shortcode_redirect(in.url, res.header("location"));
        if (location.empty()) {
            done(false, "Unknown shortcode");
            return;
        }
        const RepoInput again = parse_repo_input(location);
        if (again.kind != RepoInput::Kind::Url) {
            done(false, "The shortcode does not lead to a repository");
            return;
        }
        read_repo(again.url);
    });
}

void PluginManager::remove_repository(const std::string &url, Done done) {
    // Like RepositoryManager.removeRepository: the repository's plugins go with it.
    std::vector<std::string> ids;
    for (const auto &p : installed_) if (p.repository_url == url) ids.push_back(p.id);
    auto remaining = std::make_shared<size_t>(ids.size());
    auto finish = [this, url, done] {
        auto repos = repos_;
        repos.erase(std::remove_if(repos.begin(), repos.end(), [&](const RepositoryData &r) { return r.url == url; }),
                    repos.end());
        PluginStore *store = store_.get();
        if (!store) {
            done(false, "Storage is not available on this TV");
            return;
        }
        wnk::async::run_then_post<bool>(
            *deps_.pool, *deps_.main, life_.weak(), {},
            [store, repos](const wnk::async::CancellationToken &) { return store->save_repositories(repos, nullptr); },
            [this, repos, done](bool ok) {
                if (ok) repos_ = repos;
                changed();
                done(ok, ok ? std::string() : "Could not save repositories");
            });
    };
    if (ids.empty()) {
        finish();
        return;
    }
    for (const auto &id : ids) {
        uninstall(id, [remaining, finish](bool, const std::string &) {
            if (--*remaining == 0) finish();
        });
    }
}

void PluginManager::fetch_plugins(const RepositoryData &repo, PluginsDone done) {
    fetch(repo.url, kMaxRepoBytes, 5, [this, done](wnk::net::HttpResponse res) {
        if (!res.ok()) {
            done(false, "Could not read the repository (" + net_error(res) + ")", {});
            return;
        }
        std::string error;
        auto parsed = parse_repository(res.body, &error);
        if (!parsed) {
            done(false, error, {});
            return;
        }
        if (parsed->plugin_lists.empty()) {
            done(true, {}, {});
            return;
        }
        struct Acc {
            size_t left;
            std::vector<SitePlugin> all;
            std::string error;
        };
        auto acc = std::make_shared<Acc>(Acc{parsed->plugin_lists.size(), {}, {}});
        for (const auto &list_url : parsed->plugin_lists) {
            fetch(list_url, kMaxRepoBytes, 5, [acc, done](wnk::net::HttpResponse r) {
                if (r.ok()) {
                    std::string e;
                    auto list = parse_plugin_list(r.body, &e);
                    acc->all.insert(acc->all.end(), list.begin(), list.end());
                    if (!e.empty()) acc->error = e;
                } else {
                    acc->error = "A plugin list could not be read (" + net_error(r) + ")";
                }
                if (--acc->left == 0) done(!acc->all.empty() || acc->error.empty(), acc->error, std::move(acc->all));
            });
        }
    });
}

void PluginManager::install(const RepositoryData &repo, const SitePlugin &plugin, Done done) {
    if (plugin.disabled()) {
        done(false, "The repository marks this plugin as not working");
        return;
    }
    if (!store_) {
        done(false, "Storage is not available on this TV");
        return;
    }
    fetch(plugin.url, kMaxPluginBytes, 5, [this, repo, plugin, done](wnk::net::HttpResponse res) {
        if (!res.ok()) {
            done(false, "Download failed (" + net_error(res) + ")");
            return;
        }
        PluginStore *store = store_.get();
        const std::string dir = deps_.plugin_dir;
        auto body = std::make_shared<std::string>(std::move(res.body));
        wnk::async::run_then_post<InstallResult>(
            *deps_.pool, *deps_.main, life_.weak(), {},
            [store, repo, plugin, body, dir](const wnk::async::CancellationToken &) {
                InstallResult r;
                const std::string hash = core::Sha256::hex(*body);
                const std::string expected = expected_sha256(plugin);
                if (!expected.empty() && expected != hash) {
                    r.error = "The download does not match the repository's hash";
                    return r;
                }
                const auto zip = core::check_plugin_archive(*body);
                if (!zip.ok) {
                    r.error = "Not a valid plugin: " + zip.error;
                    return r;
                }
                InstalledPlugin rec;
                rec.id = plugin_id(repo.url, plugin.internal_name);
                rec.internal_name = plugin.internal_name;
                rec.name = plugin.name;
                rec.repository_url = repo.url;
                rec.url = plugin.url;
                rec.version = plugin.version;
                rec.sha256 = hash;
                rec.size = static_cast<long long>(body->size());
                rec.tv_types = plugin.tv_types;
                rec.language = plugin.language;
                rec.icon_url = plugin.icon_url;
                if (!store->install(rec, *body, &r.error)) return r;
                r.ok = true;
                r.record = rec;
                return r;
            },
            [this, done](InstallResult r) {
                if (!r.ok) {
                    done(false, r.error);
                    return;
                }
                WNK_LOGI(kCat, "plugins: installed %s v%d", r.record.internal_name.c_str(), r.record.version);
                bool replaced = false;
                for (auto &p : installed_) {
                    if (p.id == r.record.id) {
                        p = r.record;
                        replaced = true;
                    }
                }
                if (!replaced) installed_.push_back(r.record);
                const InstalledPlugin rec = r.record;
                unload_plugin(rec.id, [this, rec] {
                    if (runtime_ready_) load_plugin(rec);
                    changed();
                });
                done(true, {});
            });
    });
}

void PluginManager::uninstall(const std::string &id, Done done) {
    unload_plugin(id, [this, id, done] {
        PluginStore *store = store_.get();
        if (!store) {
            done(false, "Storage is not available on this TV");
            return;
        }
        const std::string path = plugin_path(id);
        wnk::async::run_then_post<bool>(
            *deps_.pool, *deps_.main, life_.weak(), {},
            [store, id, path](const wnk::async::CancellationToken &) {
                ::unlink(path.c_str());
                return store->remove(id, nullptr);
            },
            [this, id, done](bool ok) {
                if (ok) {
                    installed_.erase(std::remove_if(installed_.begin(), installed_.end(),
                                                    [&](const InstalledPlugin &p) { return p.id == id; }),
                                     installed_.end());
                }
                changed();
                done(ok, ok ? std::string() : "Could not remove the plugin");
            });
    });
}

void PluginManager::set_enabled(const std::string &id, bool enabled, Done done) {
    PluginStore *store = store_.get();
    if (!store) {
        done(false, "Storage is not available on this TV");
        return;
    }
    wnk::async::run_then_post<bool>(
        *deps_.pool, *deps_.main, life_.weak(), {},
        [store, id, enabled](const wnk::async::CancellationToken &) { return store->set_enabled(id, enabled, nullptr); },
        [this, id, enabled, done](bool ok) {
            if (!ok) {
                done(false, "Could not save");
                return;
            }
            for (auto &p : installed_) {
                if (p.id != id) continue;
                p.enabled = enabled;
                const InstalledPlugin rec = p;
                if (enabled && runtime_ready_) load_plugin(rec);
                if (!enabled) unload_plugin(id, [this] { changed(); });
            }
            changed();
            done(true, {});
        });
}

}  // namespace csweb::plugins
