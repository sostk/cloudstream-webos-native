// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/plugin_dialog_host.hpp"

#include "plugins/dialog_image.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/pill_button.hpp"

#include "wnk/log.hpp"
#include "wnk/ui/forms.hpp"

#include <algorithm>
#include <memory>

namespace csweb::ui {

using namespace wnk::ui;
using nlohmann::json;

namespace {
std::string text_of(const json &j, const char *key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
bool bool_of(const json &j, const char *key, bool def) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}
int int_of(const json &j, const char *key) {
    auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int>() : 0;
}
// The views built from a description are named after their keys, so focus can follow a redraw.
std::string view_name(const std::string &key) { return "plugin.dialog.v." + key; }
View *find_named(View *v, const std::string &name) {
    if (v->name == name) return v;
    for (const auto &c : v->children()) {
        if (View *f = find_named(c.get(), name)) return f;
    }
    return nullptr;
}
}  // namespace

void PluginDialogHost::handle(Ui &ui, const json &e) {
    const std::string ev = e.value("event", "");
    if (ev == "toast") {
        ui.toast(text_of(e, "text"), bool_of(e, "long", false) ? 3500 : 2000);
    } else if (ev == "dialog") {
        if (!bool_of(e, "answerable", true)) return;
        show(ui, e);
    } else if (ev == "dialog_closed") {
        if (current_ != nullptr && e.value("dialog", 0) == current_id_) close_current(ui);
    } else if (ev == "open_url") {
        show_link(ui, text_of(e, "url"));
    }
}

// A plugin asked to open a web page (a login page). The TV cannot hand it to a phone, so the viewer is
// shown the link to open there. The link may carry a login code: it is shown, never logged.
void PluginDialogHost::show_link(Ui &ui, const std::string &url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return;
    WNK_LOGI("csweb", "plugin asked to open a web page (shown to the viewer)");
    ui.toast("Open this on your phone or computer: " + url, 8000);
}

void PluginDialogHost::close_current(Ui &ui) {
    if (current_ == nullptr) return;
    answered_ = true;  // closed by the plugin: nothing to answer
    Dialog *d = current_;
    current_ = nullptr;
    d->dismiss(ui);
}

void PluginDialogHost::answer(int dialog, json extra) {
    json a = std::move(extra);
    json fields = json::object();
    for (const auto &f : fields_) {
        if (f.changed && !f.changed()) continue;
        fields[f.key] = f.value();
    }
    // What was done, never what was typed.
    WNK_LOGI("csweb", "plugin dialog %d answered: %s, %zu fields", dialog,
             a.contains("button") ? a["button"].get<std::string>().c_str() : a.contains("click") ? "click" : "item", fields.size());
    if (!fields.empty()) a["fields"] = std::move(fields);
    answered_ = true;
    runtime_.answer_dialog(dialog, std::move(a));
}

void PluginDialogHost::show(Ui &ui, const json &d) {
    const int id = d.value("dialog", 0);
    const bool cancelable = bool_of(d, "cancelable", true);
    const std::string heading = text_of(d, "title") + '\n' + text_of(d, "message");
    if (current_ != nullptr && id == current_id_ && heading == current_heading_) {
        // The same dialog described again, after an answer or because the plugin changed it: redrawn in
        // place, focus kept on the view with the same key (else the dialog's first focusable).
        View *f = ui.focused();
        const std::string focus = f != nullptr && ui.has_focus_within(current_) ? f->name : std::string();
        answered_ = false;
        current_->back_dismisses = cancelable;
        fill(*current_, d, id);
        View *again = focus.empty() ? nullptr : find_named(current_, focus);
        if (again == nullptr || !ui.focus(again)) ui.focus_within(current_);
        return;
    }
    if (current_ != nullptr) close_current(ui);  // a new dialog
    answered_ = false;
    WNK_LOGI("csweb", "plugin dialog %d (%s)", id, text_of(d, "stage").c_str());
    auto dialog = std::make_unique<Dialog>(text_of(d, "title"), text_of(d, "message"));
    Dialog *dlg = dialog.get();
    dlg->back_dismisses = cancelable;
    fill(*dlg, d, id);
    dlg->on_dismiss = [this, id, dlg](Ui &) {
        if (current_ == dlg) current_ = nullptr;
        if (!answered_ && dlg->back_dismisses) runtime_.answer_dialog(id, {{"button", "cancel"}});
    };
    current_ = dlg;
    current_id_ = id;
    current_heading_ = heading;
    ui.show_dialog(std::move(dialog));
}

void PluginDialogHost::fill(Dialog &dlg, const json &d, int id) {
    fields_.clear();
    dlg.body().clear_children();
    auto &scroll = dlg.body().add<ScrollView>();
    auto &body = scroll.add<Column>(12);
    if (auto v = d.find("view"); v != d.end() && v->is_object()) build(body, *v, id);
    if (auto items = d.find("items"); items != d.end() && items->is_array()) {
        const std::string choice = d.value("choice", "items");
        std::vector<int> checked;
        if (choice == "single" && d.contains("checkedItem") && d["checkedItem"].is_number_integer()) {
            checked.push_back(d["checkedItem"].get<int>());
        } else if (choice == "multiple" && d.contains("checkedItems") && d["checkedItems"].is_array()) {
            const auto &ci = d["checkedItems"];
            for (size_t i = 0; i < ci.size(); i++) {
                if (ci[i].is_boolean() && ci[i].get<bool>()) checked.push_back(static_cast<int>(i));
            }
        }
        for (size_t i = 0; i < items->size(); i++) {
            const bool on = std::find(checked.begin(), checked.end(), static_cast<int>(i)) != checked.end();
            const std::string text = (*items)[i].is_string() ? (*items)[i].get<std::string>() : std::string();
            auto &item = body.add<PillButton>(text, on ? "check" : "", PillButton::Style::Glass, [this, id, i](Ui &) {
                answer(id, {{"item", i}});
            }, 60);
            item.name = "plugin.dialog.item." + std::to_string(i);
        }
    }
    // A fixed-height scroll area only for content: a plain message dialog (PlayFy's "Restart Required")
    // would otherwise show 560 empty pixels above its buttons.
    const bool has_content = !body.children().empty();
    scroll.set_height(Dim::fixed(has_content ? 560 : 0));
    scroll.set_visible(has_content);
    // The plugin's buttons answer, then close. Not Dialog::add_button: it dismisses before its handler
    // runs, and a dismissal not preceded by an answer is a cancel.
    if (auto b = d.find("buttons"); b != d.end() && b->is_object()) {
        auto &row = dlg.body().add<wnk::ui::Row>(16);
        row.set_padding({0, 16, 0, 0});
        Dialog *dp = &dlg;
        for (const char *which : {"negative", "neutral", "positive"}) {
            const std::string text = text_of(*b, which);
            if (text.empty()) continue;
            const std::string button = which;
            auto &pb = row.add<PillButton>(text, "", std::string(which) == "positive" ? PillButton::Style::Primary : PillButton::Style::Glass,
                                           [this, id, button, dp](Ui &u) {
                                               answer(id, {{"button", button}});
                                               dp->dismiss(u);
                                           }, 60);
            pb.name = std::string("plugin.dialog.") + which;
        }
    }
}

void PluginDialogHost::build(View &parent, const json &v, int dialog) {
    const std::string type = v.value("type", "");
    const std::string key = v.value("key", "");
    const bool enabled = bool_of(v, "enabled", true);
    if (type == "column" || type == "row") {
        View *box = type == "row" ? static_cast<View *>(&parent.add<wnk::ui::Row>(12)) : &parent.add<Column>(12);
        if (auto c = v.find("children"); c != v.end() && c->is_array()) {
            for (const auto &child : *c) {
                if (child.is_object()) build(*box, child, dialog);
            }
        }
    } else if (type == "label") {
        auto &l = parent.add<Label>(text_of(v, "text"));
        l.set_max_lines(6).set_color(theme::kTextSecondary);
    } else if (type == "image") {
        // A bitmap the plugin made (Kartoons' login QR code): drawn up to 360 px, so a phone can scan it
        // from across the room.
        std::string error;
        const std::string path = plugins::write_dialog_image(image_dir_, text_of(v, "mime"), text_of(v, "data"), &error);
        const int w = int_of(v, "width"), h = int_of(v, "height");
        if (path.empty() || w <= 0 || h <= 0) {
            WNK_LOGW("csweb", "plugin dialog image not shown: %s", path.empty() ? error.c_str() : "no size");
            if (!text_of(v, "text").empty()) parent.add<Label>(text_of(v, "text")).set_color(theme::kTextSecondary);
            return;
        }
        const float scale = std::min(360.0f / static_cast<float>(w), 360.0f / static_cast<float>(h));
        auto &img = parent.add<ImageView>("file://" + path);
        img.set_fit(ImageFit::Contain).set_animate(false);
        img.set_size(static_cast<float>(w) * scale, static_cast<float>(h) * scale);
        img.name = view_name(key);
    } else if (type == "text-field") {
        auto &f = parent.add<TextField>(text_of(v, "hint"));
        const bool password = v.value("input", "") == "password";
        f.set_password(password);
        f.set_width(Dim::fixed(760));
        if (!password) f.set_text(text_of(v, "value"));
        else if (bool_of(v, "hasValue", false)) f.set_placeholder("(unchanged)");
        f.set_enabled(enabled);
        f.name = view_name(key);
        auto touched = std::make_shared<bool>(false);
        f.set_on_change([touched](Ui &, const std::string &) { *touched = true; });
        TextField *fp = &f;
        fields_.push_back({key, [fp] { return json(fp->text()); },
                           password ? std::function<bool()>([touched] { return *touched; }) : std::function<bool()>()});
    } else if (type == "checkbox" || type == "switch" || type == "toggle") {
        auto &t = parent.add<Toggle>(text_of(v, "text"), bool_of(v, "checked", false));
        t.set_style(type == "checkbox" ? Toggle::Style::Check : Toggle::Style::Switch);
        t.set_width(Dim::fixed(760));
        t.set_enabled(enabled);
        t.name = view_name(key);
        Toggle *tp = &t;
        fields_.push_back({key, [tp] { return json(tp->on()); }, {}});
    } else if (type == "button") {
        auto &b = parent.add<PillButton>(text_of(v, "text"), "", PillButton::Style::Glass, [this, dialog, key](Ui &) {
            answer(dialog, {{"click", key}});
        }, 60);
        b.set_enabled(enabled);
        b.name = view_name(key);
    } else if (type == "space") {
        parent.add<View>().set_size(1, 16);
    }
}

}  // namespace csweb::ui
