// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/plugin_dialog_host.hpp"

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
    }
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
    if (current_ != nullptr) close_current(ui);  // the same dialog described again, or a new one
    fields_.clear();
    answered_ = false;
    WNK_LOGI("csweb", "plugin dialog %d (%s)", id, text_of(d, "stage").c_str());
    auto dialog = std::make_unique<Dialog>(text_of(d, "title"), text_of(d, "message"));
    Dialog *dlg = dialog.get();
    const bool cancelable = bool_of(d, "cancelable", true);
    dlg->back_dismisses = cancelable;
    auto &scroll = dlg->body().add<ScrollView>();
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
            body.add<PillButton>(text, on ? "check" : "", PillButton::Style::Glass, [this, id, i](Ui &) {
                answer(id, {{"item", i}});
            }, 60);
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
        auto &row = dlg->body().add<wnk::ui::Row>(16);
        row.set_padding({0, 16, 0, 0});
        for (const char *which : {"negative", "neutral", "positive"}) {
            const std::string text = text_of(*b, which);
            if (text.empty()) continue;
            const std::string button = which;
            auto &pb = row.add<PillButton>(text, "", std::string(which) == "positive" ? PillButton::Style::Primary : PillButton::Style::Glass,
                                           [this, id, button, dlg](Ui &u) {
                                               answer(id, {{"button", button}});
                                               dlg->dismiss(u);
                                           }, 60);
            pb.name = std::string("plugin.dialog.") + which;
        }
    }
    dlg->on_dismiss = [this, id, cancelable, dlg](Ui &) {
        if (current_ == dlg) current_ = nullptr;
        if (!answered_ && cancelable) runtime_.answer_dialog(id, {{"button", "cancel"}});
    };
    current_ = dlg;
    current_id_ = id;
    ui.show_dialog(std::move(dialog));
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
    } else if (type == "text-field") {
        auto &f = parent.add<TextField>(text_of(v, "hint"));
        const bool password = v.value("input", "") == "password";
        f.set_password(password);
        f.set_width(Dim::fixed(760));
        if (!password) f.set_text(text_of(v, "value"));
        else if (bool_of(v, "hasValue", false)) f.set_placeholder("(unchanged)");
        f.set_enabled(enabled);
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
        Toggle *tp = &t;
        fields_.push_back({key, [tp] { return json(tp->on()); }, {}});
    } else if (type == "button") {
        auto &b = parent.add<PillButton>(text_of(v, "text"), "", PillButton::Style::Glass, [this, dialog, key](Ui &) {
            answer(dialog, {{"click", key}});
        }, 60);
        b.set_enabled(enabled);
    } else if (type == "space") {
        parent.add<View>().set_size(1, 16);
    }
}

}  // namespace csweb::ui
