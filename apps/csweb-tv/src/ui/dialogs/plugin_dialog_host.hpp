// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shows the dialogs plugins open (their settings, mostly) with the kit's widgets and sends back what
// the viewer did (docs/runtime/dialog-protocol.md). The plugin's own Android code runs unchanged in
// the runtime; this only draws the description and answers it. A dialog that stays open after an
// answer, or that the plugin changes by itself (a login code arriving, issue #38), is described again
// and redrawn in place, keeping focus. Field values, answers and links are never logged.
#pragma once

#include "runtime/runtime_client.hpp"

#include "wnk/ui/navigation.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace csweb::ui {

class PluginDialogHost {
 public:
    // `image_dir`: where images in dialogs are written for the ImageView (the app's tmpfs).
    PluginDialogHost(runtime::RuntimeClient &runtime, std::string image_dir)
        : runtime_(runtime), image_dir_(std::move(image_dir)) {}
    // dialog, dialog_closed, toast and open_url events (RuntimeClient::on_dialog).
    void handle(wnk::ui::Ui &ui, const nlohmann::json &event);

 private:
    struct Field {
        std::string key;
        std::function<nlohmann::json()> value;
        std::function<bool()> changed;  // a password field is sent only when typed into
    };
    void show(wnk::ui::Ui &ui, const nlohmann::json &d);
    // The dialog's content (view tree, list, buttons) into its body; fields_ from it.
    void fill(wnk::ui::Dialog &dlg, const nlohmann::json &d, int id);
    void build(wnk::ui::View &parent, const nlohmann::json &view, int dialog);
    void show_link(wnk::ui::Ui &ui, const std::string &url);
    void answer(int dialog, nlohmann::json extra);
    void close_current(wnk::ui::Ui &ui);

    runtime::RuntimeClient &runtime_;
    std::string image_dir_;
    wnk::ui::Dialog *current_ = nullptr;
    int current_id_ = 0;
    std::string current_heading_;  // title + message of the shown dialog: in-place redraw only when unchanged
    bool answered_ = false;  // the shown dialog was answered: its dismissal is not a cancel
    std::vector<Field> fields_;
};

}  // namespace csweb::ui
