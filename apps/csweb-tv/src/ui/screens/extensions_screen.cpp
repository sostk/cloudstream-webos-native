// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/extensions_screen.hpp"

#include "core/guard.hpp"
#include "plugins/settings_support.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/pill_button.hpp"

#include "wnk/log.hpp"

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr const char *kCat = "csweb";

// Rows are rebuilt whenever a plugin changes; focus goes back to the row it was on, found by the
// suffix every focusable view of a row shares (".<internal name>").
View *find_by_suffix(View &root, const std::string &suffix) {
    if (root.focusable() && root.name.size() >= suffix.size() &&
        root.name.compare(root.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return &root;
    }
    for (const auto &c : root.children()) {
        if (View *v = find_by_suffix(*c, suffix)) return v;
    }
    return nullptr;
}
std::string row_suffix(const std::string &name) {
    const size_t dot = name.find('.', name.find('.') + 1);  // "plugin.<action>.<internal>"
    return dot == std::string::npos ? std::string() : name.substr(dot);
}

Column &page(View &screen, const std::string &title, const std::string &intro) {
    auto &scroll = screen.add<ScrollView>();
    scroll.margin = 160;
    auto &col = scroll.add<Column>(22);
    col.set_padding({theme::kSafeX, 80, theme::kSafeX, 80});
    col.add<Label>(title, TextRole::Title).set_font_size(theme::kDisplay).set_bold(true);
    if (!intro.empty()) col.add<Label>(intro).set_color(theme::kTextSecondary).set_max_lines(3).set_width(Dim::fixed(1400));
    return col;
}

// A row: text block on the left (title, detail lines), buttons on the right.
struct ItemRow {
    wnk::ui::Row *row;
    Column *text;
    wnk::ui::Row *buttons;
};
ItemRow item_row(Column &list, const std::string &title, const std::string &detail) {
    auto &row = list.add<wnk::ui::Row>(20);
    row.set_width(Dim::fill());
    row.set_padding({24, 18, 24, 18});
    row.set_background(theme::kSurface, theme::kRadius);
    row.set_focus_scope();
    auto &text = row.add<Column>(6);
    text.set_width(Dim::fill());
    text.add<Label>(title).set_bold(true);
    if (!detail.empty()) text.add<Label>(detail, TextRole::Small).set_color(theme::kTextMuted).set_max_lines(2);
    auto &buttons = row.add<wnk::ui::Row>(12);
    buttons.set_align(Align::End, Align::Center);
    return {&row, &text, &buttons};
}
}  // namespace

// ---- Extensions ---------------------------------------------------------------------------------------

ExtensionsScreen::ExtensionsScreen(app::Services &s) : Screen("Extensions"), s_(s) {
    name = "extensions";
    auto &col = page(*this, "Extensions",
                     "Add the repositories you trust, then install their plugins. Plugins run on this TV inside "
                     "the plugin runtime; this app does not provide any.");
    auto &actions = col.add<wnk::ui::Row>(16);
    actions.add<PillButton>("Add repository", "plus", PillButton::Style::Primary, [this](Ui &ui) { open_add(ui); }, 64)
        .name = "extensions.add";
    summary_ = &col.add<Label>("");
    summary_->set_muted(true);
    list_ = &col.add<Column>(14);
    list_->set_width(Dim::fill());
    sub_ = s_.plugins_changed.subscribe([this] { rebuild(); });
}

void ExtensionsScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    rebuild();
}

void ExtensionsScreen::rebuild() {
    list_->clear_children();
    const auto &repos = s_.plugins->repositories();
    const auto &installed = s_.plugins->installed();
    summary_->set_text(std::to_string(repos.size()) + (repos.size() == 1 ? " repository · " : " repositories · ") +
                       std::to_string(installed.size()) + " plugins installed · " +
                       std::to_string(s_.plugins->providers().size()) + " providers loaded" +
                       (s_.plugins->loading() ? " (loading…)" : ""));
    if (repos.empty()) {
        list_->add<Label>("No repositories yet.").set_muted(true);
    }
    for (const auto &r : repos) {
        size_t count = 0;
        for (const auto &p : installed) count += p.repository_url == r.url;
        auto row = item_row(*list_, r.name.empty() ? r.url : r.name,
                            r.url + "  ·  " + std::to_string(count) + " installed");
        const plugins::RepositoryData repo = r;
        auto &open = row.buttons->add<PillButton>("Plugins", "chevron-right", PillButton::Style::Glass,
                                                  [this, repo](Ui &ui) { s_.router->open_repository(ui, repo); }, 60);
        open.name = "extensions.repo." + r.name;
        row.buttons->add<PillButton>("", "trash", PillButton::Style::Glass, [this, repo](Ui &ui) { confirm_remove(ui, repo); }, 60)
            .accessible_label = "Remove repository";
    }
    // Plugins whose repository is gone (or that failed to load) stay visible here.
    std::vector<const plugins::InstalledPlugin *> failed;
    for (const auto &p : installed) {
        auto it = s_.plugins->load_errors().find(p.id);
        if (it != s_.plugins->load_errors().end() && !it->second.empty()) failed.push_back(&p);
    }
    if (!failed.empty()) {
        list_->add<Label>("Plugins that did not load").set_bold(true).set_font_size(theme::kHeadline);
        for (const auto *p : failed) {
            item_row(*list_, p->name, s_.plugins->load_errors().at(p->id));
        }
    }
}

void ExtensionsScreen::open_add(Ui &ui) {
    auto d = std::make_unique<Dialog>("Add repository", "A repository URL, a cloudstreamrepo:// link or a shortcode.");
    Dialog *dlg = d.get();
    auto &field = d->body().add<TextField>("https://… or shortcode");
    field.set_width(Dim::fixed(800));
    field.name = "extensions.add.url";
    TextField *f = &field;
    auto submit = [this, f](Ui &u) {
        const std::string typed = f->text();
        if (typed.empty()) return;
        u.toast("Adding repository…", 1500);
        s_.plugins->add_repository(typed, core::guarded(life_.weak(), [this](bool ok, const std::string &error) {
            if (Ui *v = this->ui()) v->toast(ok ? "Repository added" : error, ok ? 2000 : 4000);
            rebuild();
        }));
    };
    field.set_on_submit([submit, dlg](Ui &u, const std::string &) {
        submit(u);
        dlg->dismiss(u);
    });
    d->add_button("Cancel");
    d->add_button("Add", submit).name = "extensions.add.ok";
    ui.show_dialog(std::move(d));
    ui.focus(f);
}

void ExtensionsScreen::confirm_remove(Ui &ui, const plugins::RepositoryData &repo) {
    auto d = std::make_unique<Dialog>("Remove " + (repo.name.empty() ? repo.url : repo.name) + "?",
                                      "Its installed plugins are removed too.");
    d->add_button("Cancel");
    d->add_button("Remove", [this, repo](Ui &) {
        s_.plugins->remove_repository(repo.url, core::guarded(life_.weak(), [this](bool ok, const std::string &error) {
            if (!ok) {
                if (Ui *u = this->ui()) u->toast(error, 4000);
            }
            rebuild();
        }));
    });
    ui.show_dialog(std::move(d));
}

// ---- Repository -----------------------------------------------------------------------------------------

RepositoryScreen::RepositoryScreen(app::Services &s, plugins::RepositoryData repo)
    : Screen(repo.name), s_(s), repo_(std::move(repo)) {
    name = "repository";
    auto &col = page(*this, repo_.name.empty() ? "Repository" : repo_.name, repo_.url);
    status_ = &col.add<Label>("Loading plugins…");
    status_->set_muted(true).set_max_lines(3);
    list_ = &col.add<Column>(14);
    list_->set_width(Dim::fill());
    sub_ = s_.plugins_changed.subscribe([this] { rebuild(); });
}

void RepositoryScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (plugins_.empty() && !fetching_) fetch();
}

void RepositoryScreen::fetch() {
    fetching_ = true;
    s_.plugins->fetch_plugins(repo_, core::guarded(life_.weak(), [this](bool ok, const std::string &error,
                                                                         std::vector<plugins::SitePlugin> list) {
        fetching_ = false;
        error_ = ok ? std::string() : error;
        plugins_ = std::move(list);
        std::stable_sort(plugins_.begin(), plugins_.end(),
                         [](const auto &a, const auto &b) { return a.name < b.name; });
        rebuild();
    }));
}

void RepositoryScreen::rebuild() {
    Ui *ui = this->ui();
    std::string focus_suffix;
    if (ui && ui->focused() && list_->is_ancestor_of(ui->focused())) focus_suffix = row_suffix(ui->focused()->name);
    list_->clear_children();
    if (fetching_) return;
    size_t installed_count = 0;
    for (const auto &sp : plugins_) installed_count += s_.plugins->installed_plugin(repo_.url, sp.internal_name) != nullptr;
    status_->set_text(!error_.empty() ? error_
                                      : std::to_string(plugins_.size()) + " plugins · " + std::to_string(installed_count) +
                                            " installed");
    for (const auto &sp : plugins_) {
        const plugins::InstalledPlugin *ip = s_.plugins->installed_plugin(repo_.url, sp.internal_name);
        std::string detail = "v" + std::to_string(sp.version);
        if (!sp.language.empty()) detail += "  ·  " + sp.language;
        for (const auto &t : sp.tv_types) detail += "  ·  " + t;
        if (sp.disabled()) detail += "  ·  marked as not working";
        if (ip) {
            auto it = s_.plugins->load_errors().find(ip->id);
            if (it != s_.plugins->load_errors().end() && !it->second.empty()) detail += "  ·  did not load: " + it->second;
            else if (!ip->enabled) detail += "  ·  disabled";
            else detail += "  ·  installed v" + std::to_string(ip->version);
        }
        if (!sp.description.empty()) detail += "\n" + sp.description;
        auto row = item_row(*list_, sp.name.empty() ? sp.internal_name : sp.name, detail);
        const plugins::SitePlugin plugin = sp;
        if (busy_ == sp.internal_name) {
            // Focusable, so focus stays on this row while the work runs.
            row.buttons->add<PillButton>(ip ? "Working…" : "Installing…", "", PillButton::Style::Glass, ClickHandler{}, 60)
                .name = "plugin.busy." + sp.internal_name;
            continue;
        }
        auto finish = [this](const char *what) {
            return core::guarded(life_.weak(), [this, what](bool ok, const std::string &error) {
                busy_.clear();
                if (Ui *u = this->ui()) u->toast(ok ? std::string(what) : error, ok ? 2000 : 4500);
                rebuild();
            });
        };
        if (!ip) {
            auto &b = row.buttons->add<PillButton>("Install", "download", PillButton::Style::Glass, [this, plugin, finish](Ui &) {
                busy_ = plugin.internal_name;
                s_.plugins->install(repo_, plugin, finish("Installed"));
                rebuild();
            }, 60);
            b.name = "plugin.install." + sp.internal_name;
            b.set_enabled(!sp.disabled());
            continue;
        }
        if (plugins::is_outdated(sp.version, ip->version)) {
            row.buttons->add<PillButton>("Update", "refresh", PillButton::Style::Glass, [this, plugin, finish](Ui &) {
                busy_ = plugin.internal_name;
                s_.plugins->install(repo_, plugin, finish("Updated"));
                rebuild();
            }, 60).name = "plugin.update." + sp.internal_name;
        }
        const plugins::InstalledPlugin installed = *ip;
        auto &settings = row.buttons->add<PillButton>("", "settings", PillButton::Style::Glass,
                                                      [this, installed](Ui &ui) { open_settings(ui, installed); }, 60);
        settings.accessible_label = "Plugin settings";
        settings.name = "plugin.settings." + sp.internal_name;
        auto &remove = row.buttons->add<PillButton>("", "trash", PillButton::Style::Glass, [this, installed, finish](Ui &) {
            busy_ = installed.internal_name;
            s_.plugins->uninstall(installed.id, finish("Removed"));
            rebuild();
        }, 60);
        remove.accessible_label = "Remove plugin";
        remove.name = "plugin.remove." + sp.internal_name;
    }
    if (ui && !focus_suffix.empty()) {
        if (View *v = find_by_suffix(*list_, focus_suffix)) ui->focus(v);
    }
}

void RepositoryScreen::open_settings(Ui &ui, const plugins::InstalledPlugin &p) {
    // Plugin.openSettings runs in the runtime; its dialogs reach PluginDialogHost. When it ends the
    // plugin is loaded again so what it saved takes effect.
    auto opened = std::make_shared<bool>(false);
    const std::string id = p.id;
    s_.runtime->request({{"op", "settings"}, {"plugin", s_.plugins->plugin_path(p.id)}},
                        [opened](const nlohmann::json &e) {
                            const std::string ev = e.value("event", "");
                            if (ev == "dialog") *opened = true;
                            if (ev == "settings" && e.value("available", false)) *opened = true;
                        },
                        core::guarded(life_.weak(), [this, opened, id](bool ok, const std::string &error) {
                            Ui *u = this->ui();
                            WNK_LOGI(kCat, "plugin settings ended: %s%s", ok ? "ok" : "failed: ", ok ? "" : error.c_str());
                            if (!ok && plugins::settings_need_activity(error)) {
                                WNK_LOGW(kCat, "plugin settings: unsupported (needs an Android Activity/fragment)");
                            }
                            if (!ok && u) u->toast(plugins::settings_failure_message(error), 5000);
                            else if (!*opened && u) u->toast("This plugin has no settings.");
                            if (*opened) {
                                s_.plugins->reload(id);
                                s_.plugin_data->save(s_.ctx->pool());
                            }
                        }));
    ui.toast("Opening settings…", 1200);
}

}  // namespace csweb::ui
