// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/application.hpp"

#include "app/deep_link.hpp"
#include "player/link_priority.hpp"
#include "runtime/network_settings.hpp"
#include "runtime/runtime_launcher.hpp"
#include "settings/preferences.hpp"
#include "ui/screens/home_screen.hpp"
#include "ui/screens/player_screen.hpp"
#include "ui/screens/setup_screen.hpp"
#include "ui/screens/splash_screen.hpp"
#include "ui/widgets/glass.hpp"
#include "ui/widgets/icons.hpp"

#include "wnk/log.hpp"
#include "wnk/media/ndl_backend.hpp"
#include "wnk/platform/process.hpp"

#include <cstdlib>
#include <sys/stat.h>

#ifndef CSWEB_HOST_RUNTIME_DIR
#define CSWEB_HOST_RUNTIME_DIR ""
#endif
// The legacy package (CSWEB_LEGACY, docs/legacy-build.md) carries an armv7 runtime under its own app id.
#ifdef CSWEB_LEGACY
#define CSWEB_RUNTIME_SUBDIR "/runtime-armv7"
#define CSWEB_TMP_FALLBACK "/tmp/org.csweb.tv.legacy"
#else
#define CSWEB_RUNTIME_SUBDIR "/runtime-aarch64"
#define CSWEB_TMP_FALLBACK "/tmp/org.csweb.tv"
#endif

namespace csweb::app {
#ifdef CSWEB_LEGACY_CURL_DIAGNOSTICS
void log_legacy_curl_diagnostics();
#endif

namespace {
constexpr const char *kCat = "csweb";

void make_dir(const std::string &path) { ::mkdir(path.c_str(), 0700); }

std::string runtime_dir(const std::string &app_dir) {
    if (const char *env = std::getenv("CSWEB_RUNTIME_DIR"); env && *env) return env;
    if (wnk::platform::kIsWebOS) return app_dir + CSWEB_RUNTIME_SUBDIR;
    return CSWEB_HOST_RUNTIME_DIR;
}
}  // namespace

Application::Application() = default;

Application::~Application() {
    // on_stop has taken the screens down; stop the runtime so no answer arrives for a dead manager.
    if (runtime_) runtime_->stop();
    if (plugin_data_) plugin_data_->save_now();
}

void Application::on_start(wnk::app::Context &ctx) {
#ifdef CSWEB_LEGACY_CURL_DIAGNOSTICS
    log_legacy_curl_diagnostics();
#endif
    const std::string app_dir = ctx.launch().app_dir;
    std::string tmp = ctx.runtime_dir();
    if (tmp.empty()) tmp = CSWEB_TMP_FALLBACK;
    make_dir(tmp);
    stalls_ = std::make_unique<core::StallSampler>(ctx.dispatcher(), tmp + "/stalls.log");
    const std::string plugin_dir = tmp + "/plugins", data_dir = tmp + "/plugin-data";
    make_dir(plugin_dir);
    make_dir(data_dir);
    ui::set_asset_root(app_dir);
    ui::glass::init(ctx.images(), &ctx.pool(), &ctx.dispatcher());
    services_.ctx = &ctx;

    settings_ = std::make_unique<settings::SettingsStore>(ctx.store(), ctx.pool());
    settings_->load();
    services_.settings = settings_.get();
    // The profile chosen last time (DataStoreHelper.selectedKeyIndex) decides which library and
    // per-profile settings are used.
    services_.profiles = profiles::Profiles::from_json(settings_->get(settings::kProfiles));
    settings_->set_profile(services_.profiles.selected());
    library_ = std::make_unique<library::LibraryStore>(ctx.store(), ctx.pool());
    if (services_.profiles.selected() != 0) library_->switch_profile(services_.profiles.selected());
    else library_->load();
    services_.library = library_.get();

    if (auto transport = ctx.http()) {
        http_ = std::make_unique<wnk::net::HttpClient>(transport, ctx.pool(), ctx.dispatcher());
        services_.http = http_.get();
        wnk::net::CurlOptions po;
        po.cookies = true;
        po.user_agent = player::kCloudStreamUserAgent;
        services_.player_http = wnk::net::make_curl_transport(po);
    }

    const std::string rt_dir = runtime_dir(app_dir);
    WNK_LOGI(kCat, "csweb: app %s, runtime %s, tmp %s", app_dir.c_str(), rt_dir.c_str(), tmp.c_str());
    auto &dispatcher = ctx.dispatcher();
    runtime_ = std::make_unique<runtime::RuntimeClient>(
        [rt_dir, tmp, data_dir](core::ChildSpec *spec, std::string *error) {
            runtime::LaunchPlan plan = runtime::plan_runtime_launch(rt_dir, tmp, data_dir);
            if (!plan.ok) {
                *error = plan.error;
                return false;
            }
            WNK_LOGI(kCat, "runtime: launching (AOT: %s)", plan.aot_status.c_str());
            *spec = std::move(plan.spec);
            return true;
        },
        [&dispatcher](std::function<void()> fn) { dispatcher.post(std::move(fn)); });
    services_.runtime = runtime_.get();

    plugins::PluginManager::Deps deps;
    deps.http = ctx.http();
    deps.pool = &ctx.pool();
    deps.main = &ctx.dispatcher();
    deps.kv = ctx.store();
    deps.runtime = runtime_.get();
    deps.plugin_dir = plugin_dir;
    deps.home_provider = [this] { return settings_ ? settings_->get_string(settings::kHomeProvider) : std::string(); };
    plugins_ = std::make_unique<plugins::PluginManager>(std::move(deps));
    services_.plugins = plugins_.get();
    plugins_->on_changed = [this] { services_.plugins_changed.emit(); };

    plugin_data_ = std::make_unique<plugins::PluginDataStore>(ctx.store(), data_dir);
    plugin_data_->restore();
    services_.plugin_data = plugin_data_.get();

    runtime_->on_ready = [this] {
        // Before any load: a plugin's load() may already fetch through the chosen resolver.
        if (settings_) runtime::send_dns(*runtime_, settings::dns_value(*settings_));
        plugins_->on_runtime_ready();
    };
    runtime_->on_state = [this](runtime::RuntimeClient::State) {
        services_.runtime_changed.emit();
        services_.plugins_changed.emit();  // Home shows a runtime failure
    };
    dialogs_ = std::make_unique<ui::PluginDialogHost>(*runtime_);
    runtime_->on_dialog = [this, &ctx](const nlohmann::json &e) { dialogs_->handle(ctx.ui(), e); };

    if (wnk::platform::kIsWebOS) {
        // A Luna call: on a worker, ready long before the first link plays.
        wnk::async::run_then_post<wnk::media::DeviceMediaCaps>(
            ctx.pool(), ctx.dispatcher(), life_.weak(), {},
            [](const wnk::async::CancellationToken &) { return wnk::media::DeviceMediaCaps::detect(); },
            [this](wnk::media::DeviceMediaCaps caps) {
                WNK_LOGI(kCat, "device media: %s", caps.summary().c_str());
                services_.device = std::move(caps);
            });
    }

    wnk::ui::Ui &ui = ctx.ui();
    nav_ = &ui.root().add<wnk::ui::Navigator>();
    router_ = std::make_unique<Router>(services_, *nav_);
    services_.router = router_.get();
    nav_->push(ui, std::make_unique<ui::HomeScreen>(services_));

    plugins_->start();
    runtime_->start();
    plugins_sub_ = services_.plugins_changed.subscribe([this, &ctx] { run_pending_search(ctx.ui()); });
    if (!settings_->get_bool(settings::kSetupDone, false)) {
        // First run (CloudStream's setup until HAS_DONE_SETUP_KEY): links wait for it.
        choosing_profile_ = true;
        nav_->push(ui, std::make_unique<ui::SetupScreen>(services_, [this](wnk::ui::Ui &u, bool open_extensions) {
            choosing_profile_ = false;
            router_->back(u);
            if (open_extensions) router_->open_extensions(u);
            run_pending_search(u);
        }));
    } else {
        // AccountSelectActivity: the picker at start with more than one profile (unless skipped), and
        // always for a locked profile. Links wait until a profile is chosen.
        const bool many = services_.profiles.all().size() > 1 && !settings_->get_bool(settings::kSkipProfileSelect, false);
        if (many || services_.profiles.current().locked()) {
            choosing_profile_ = true;
            router_->open_profiles(ui, true, [this](wnk::ui::Ui &u) {
                choosing_profile_ = false;
                run_pending_search(u);
            });
        }
    }
    // The splash over all of it (CloudStream's splash_background); it fades into the screen below.
    if (std::getenv("CSWEB_NO_SPLASH") == nullptr) {
        nav_->push(ui, std::make_unique<ui::SplashScreen>([this](wnk::ui::Ui &u) { router_->back(u); }));
    }
    open_link(ui, ctx.launch().params_json);
}

void Application::on_frame(wnk::app::Context &ctx, float dt) {
    since_data_save_s_ += dt;
    if (since_data_save_s_ < 300) return;
    since_data_save_s_ = 0;
    if (plugin_data_) plugin_data_->save(ctx.pool());
}

void Application::on_relaunch(wnk::app::Context &ctx, const std::string &params_json) {
    open_link(ctx.ui(), params_json);
}

// MainActivity.handleAppIntentUrl: a repository link adds the repository at once (loadRepository),
// then offers to open it (addRepositoryDialog); a search link opens Search with the query once the
// plugins have loaded.
void Application::open_link(wnk::ui::Ui &ui, const std::string &params_json) {
    const DeepLink link = parse_deep_link(params_json);
    if (link.kind == DeepLink::Kind::Search) {
        pending_search_ = link.value;
        run_pending_search(ui);
    } else if (link.kind == DeepLink::Kind::Repository) {
        pending_repo_ = link.value;
        run_pending_search(ui);
    }
}

void Application::add_repository_from_link(wnk::ui::Ui &ui, const std::string &url) {
    WNK_LOGI(kCat, "link: add repository");
    plugins_->add_repository(url, [this, &ui, url](bool ok, const std::string &error) {
        if (!ok) {
            WNK_LOGW(kCat, "link: repository not added: %s", error.c_str());
            ui.toast(error, 4000);
            return;
        }
        const plugins::RepositoryData *repo = nullptr;
        for (const auto &r : plugins_->repositories()) {
            if (r.url == url) repo = &r;
        }
        if (!repo || !nav_) return;
        WNK_LOGI(kCat, "link: repository added");
        const plugins::RepositoryData data = *repo;
        auto d = std::make_unique<wnk::ui::Dialog>("Repository added", data.name.empty() ? data.url : data.name);
        d->add_button("Close");
        d->add_button("Open repository", [this, data](wnk::ui::Ui &u) {
            router_->open_extensions(u);
            router_->open_repository(u, data);
        }).name = "link.open";
        ui.show_dialog(std::move(d));
    });
}

// Links wait for what they need: a repository for the stored list to be read (or the add would be
// overwritten), a search for the plugins to load.
void Application::run_pending_search(wnk::ui::Ui &ui) {
    if (choosing_profile_) return;
    if (!pending_repo_.empty() && plugins_->started()) {
        const std::string url = std::move(pending_repo_);
        pending_repo_.clear();
        add_repository_from_link(ui, url);
    }
    if (pending_search_.empty() || plugins_->loading() || !router_) return;
    const std::string q = std::move(pending_search_);
    pending_search_.clear();
    router_->open_search(ui, q);
}

void Application::on_lifecycle(wnk::app::Context &ctx, const wnk::app::Lifecycle::Transition &t) {
    auto *player = nav_ ? dynamic_cast<ui::PlayerScreen *>(nav_->top()) : nullptr;
    if (t.to == wnk::app::State::Suspended) {
        if (plugin_data_) plugin_data_->save(ctx.pool());  // the TV may end a background app without notice
        if (player) player->pause_for_background();
        if (wnk::platform::kIsWebOS) wnk::media::NdlBackend::set_app_foreground(false);
    } else if (t.to == wnk::app::State::Resuming && wnk::platform::kIsWebOS) {
        wnk::media::NdlBackend::set_app_foreground(true);
    } else if (t.from == wnk::app::State::Resuming && t.to == wnk::app::State::Running) {
        if (player) player->return_from_background();
    }
}

void Application::on_stop(wnk::app::Context &ctx) {
    stalls_.reset();  // the dispatcher stops running pings during shutdown
    wnk::ui::Ui &ui = ctx.ui();
    while (wnk::ui::Dialog *d = ui.top_dialog()) ui.dismiss_dialog(d);
    // The shell destroys the delegate before the Ui, and every screen refers to the services: take
    // the screens down now (the player stops, requests are cancelled) while the services exist.
    if (nav_) {
        std::unique_ptr<wnk::ui::View> screens = ui.root().take_child(nav_);
        nav_ = nullptr;
        screens.reset();
    }
    if (runtime_) runtime_->stop();
    if (plugin_data_) plugin_data_->save_now();
}

}  // namespace csweb::app
