// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/profiles_screen.hpp"

#include "profiles/profiles.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/glass_dialog.hpp"
#include "ui/widgets/pill_button.hpp"
#include "ui/widgets/pin_pad.hpp"
#include "ui/widgets/profile_avatar.hpp"

#include "wnk/log.hpp"

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr const char *kCat = "csweb";

void prompt_pin(Ui &ui, const std::string &title, const std::string &message,
                std::function<void(Ui &, const std::string &)> done) {
    auto d = std::make_unique<GlassDialog>(title, message);
    Dialog *dlg = d.get();
    auto &field = d->body().add<TextField>("4 digits");
    field.set_password(true).set_max_length(4);
    field.set_width(Dim::fixed(500));
    field.name = "profiles.pin";
    TextField *f = &field;
    field.set_on_submit([done, dlg](Ui &u, const std::string &pin) {
        dlg->dismiss(u);
        done(u, pin);
    });
    d->add_button("Cancel");
    d->add_button("OK", [done, f](Ui &u) { done(u, f->text()); }).name = "profiles.pin.ok";
    ui.show_dialog(std::move(d));
    ui.focus(f);
}
}  // namespace

void ask_profile_name(Ui &ui, const std::string &title, const std::string &current,
                      std::function<void(Ui &, const std::string &)> done) {
    auto d = std::make_unique<GlassDialog>(title);
    Dialog *dlg = d.get();
    auto &field = d->body().add<TextField>("Name");
    field.set_max_length(40);
    field.set_text(current);
    field.set_width(Dim::fixed(700));
    field.name = "profiles.name";
    TextField *f = &field;
    field.set_on_submit([done, dlg](Ui &u, const std::string &name) {
        dlg->dismiss(u);
        done(u, name);
    });
    d->add_button("Cancel");
    d->add_button("OK", [done, f](Ui &u) { done(u, f->text()); }).name = "profiles.name.ok";
    ui.show_dialog(std::move(d));
    ui.focus(f);
}

void choose_profile_colour(Ui &ui, app::Services &s, int key_index, std::function<void(Ui &)> changed) {
    auto d = std::make_unique<GlassDialog>("Colour");
    // Six avatars with their focus rings need more than the standard panel's content width.
    d->set_panel_width(1060);
    Dialog *dlg = d.get();
    auto &row = d->body().add<wnk::ui::Row>(24);
    row.set_focus_scope();
    const profiles::Profile *p = s.profiles.find(key_index);
    View *current = nullptr;
    for (int c = 0; c < profiles::kAvatarColours; c++) {
        auto &a = row.add<ProfileAvatar>("", p ? profiles::initials(p->name) : "", c, [&s, dlg, key_index, c, changed](Ui &u) {
            if (s.profiles.set_image(key_index, c)) s.settings->set(settings::kProfiles, s.profiles.to_json());
            s.profile_changed.emit();
            dlg->dismiss(u);
            if (changed) changed(u);
        }, 96);
        a.name = "profiles.colour." + std::to_string(c);
        a.accessible_label = "Colour " + std::to_string(c + 1);
        if (p && p->image_index == c) current = &a;
    }
    d->add_button("Cancel");
    ui.show_dialog(std::move(d));
    if (current) ui.focus(current);
}

void unlock_profile(Ui &ui, app::Services &s, int key_index, std::function<void(Ui &)> then) {
    const profiles::Profile *p = s.profiles.find(key_index);
    if (p == nullptr) return;
    if (!p->locked()) {
        then(ui);
        return;
    }
    const std::string name = p->name;
    prompt_pin(ui, "Enter the PIN for " + name, "", [&s, key_index, then, name](Ui &u, const std::string &pin) {
        if (!s.profiles.check_pin(key_index, pin)) {
            u.toast("Wrong PIN for " + name + ".");  // the PIN itself is never logged or shown
            return;
        }
        then(u);
    });
}

ProfilesScreen::ProfilesScreen(app::Services &s, bool startup, std::function<void(Ui &)> on_chosen)
    : Screen("Profiles"), s_(s), startup_(startup), on_chosen_(std::move(on_chosen)) {
    name = "profiles";
    set_background(theme::kBackground);
    auto &col = add<Column>(34);
    col.set_width(Dim::fill()).set_height(Dim::fill());
    col.set_padding({theme::kSafeX, 150, theme::kSafeX, 80});
    col.set_align(Align::Center, Align::Start);
    auto &title = col.add<Label>("Who's watching?", TextRole::Title);
    title.set_font_size(56).set_bold(true);
    title.set_align(Align::Center, Align::Start);
    hint_ = &col.add<Label>("");
    hint_->set_color(theme::kTextMuted).set_font_size(theme::kLabel);
    hint_->set_align(Align::Center, Align::Start);
    tiles_ = &col.add<wnk::ui::Row>(64);
    tiles_->set_align(Align::Center, Align::Start);
    tiles_->set_padding({0, 26, 0, 0});
    tiles_->set_focus_scope();
    pin_ = &col.add<PinPad>([this](Ui &ui, const std::string &pin) {
        const int key = pin_for_;
        if (!s_.profiles.check_pin(key, pin)) {
            pin_->show_error("Wrong PIN. Try again.");  // the PIN itself is never logged or shown
            return;
        }
        close_pin(ui);
        auto chosen = on_chosen_;
        const bool startup = startup_;
        s_.router->switch_profile(ui, key);  // pops this screen
        if (startup && chosen) chosen(ui);
    });
    pin_->set_padding({0, 30, 0, 0});
    pin_->set_align(Align::Center, Align::Start);
    pin_->set_visible(false);

    auto &corner = add<wnk::ui::Row>(0);
    corner.set_align(Align::End, Align::End);
    corner.set_padding({0, 0, theme::kSafeX, 64});
    manage_ = &corner.add<PillButton>("Manage profiles", "settings", PillButton::Style::Glass, [this](Ui &ui) {
        managing_ = !managing_;
        close_pin(ui);
        rebuild();
        if (first_) ui.focus(first_);
    }, 60);
    manage_->name = "profiles.manage";
    rebuild();
}

void ProfilesScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (first_) ui.focus(first_);
}

bool ProfilesScreen::on_back(Ui &ui) {
    if (pin_for_ >= 0) {
        close_pin(ui);
        return true;
    }
    if (managing_) {
        managing_ = false;
        rebuild();
        if (first_) ui.focus(first_);
        return true;
    }
    if (startup_) {
        // AccountSelectActivity is the first screen: Back leaves instead of skipping the choice.
        if (s_.ctx) s_.ctx->quit("back on profile selection");
        return true;
    }
    return false;
}

void ProfilesScreen::save() { s_.settings->set(settings::kProfiles, s_.profiles.to_json()); }

void ProfilesScreen::rebuild() {
    tiles_->clear_children();
    first_ = nullptr;
    hint_->set_text(managing_ ? "Choose a profile to change it."
                              : "Each profile has its own library, home source and searches.");
    manage_->set_text(managing_ ? "Done" : "Manage profiles");
    manage_->set_icon(managing_ ? "check" : "settings");
    for (const auto &p : s_.profiles.all()) {
        const int key = p.key_index;
        auto &a = tiles_->add<ProfileAvatar>(p.name, profiles::initials(p.name), p.image_index, [this, key](Ui &ui) {
            if (managing_) edit(ui, key);
            else choose(ui, key);
        });
        a.set_badge(managing_ ? ProfileAvatar::Badge::Edit : p.locked() ? ProfileAvatar::Badge::Lock : ProfileAvatar::Badge::None);
        a.set_current(!managing_ && key == s_.profiles.selected() && s_.profiles.all().size() > 1);
        a.name = "profiles.profile." + std::to_string(key);
        if (first_ == nullptr || key == s_.profiles.selected()) first_ = &a;
    }
    auto &add = tiles_->add<ProfileAvatar>("Add", "+", -1, [this](Ui &ui) { add_profile(ui); });
    add.name = "profiles.add";
    add.accessible_label = "Add profile";
}

void ProfilesScreen::add_profile(Ui &ui) {
    ask_profile_name(ui, "New profile", "", [this](Ui &u, const std::string &name) {
        if (name.empty()) return;
        const int key = s_.profiles.add(name).key_index;
        save();
        WNK_LOGI(kCat, "profiles: added %d", key);
        rebuild();
        for (const auto &v : tiles_->children()) {
            if (v->name == "profiles.profile." + std::to_string(key)) u.focus(v.get());
        }
    });
}

void ProfilesScreen::choose(Ui &ui, int key_index) {
    const profiles::Profile *p = s_.profiles.find(key_index);
    if (p == nullptr) return;
    if (p->locked()) {
        open_pin(ui, key_index);
        return;
    }
    auto chosen = on_chosen_;
    const bool startup = startup_;
    s_.router->switch_profile(ui, key_index);  // pops this screen
    if (startup && chosen) chosen(ui);
}

void ProfilesScreen::open_pin(Ui &ui, int key_index) {
    const profiles::Profile *p = s_.profiles.find(key_index);
    if (p == nullptr) return;
    pin_for_ = key_index;
    pin_->clear();
    pin_->set_prompt("Enter the PIN for " + p->name);
    pin_->set_visible(true);
    if (pin_->first_key()) ui.focus(pin_->first_key());
}

void ProfilesScreen::close_pin(Ui &ui) {
    if (pin_for_ < 0) return;
    const int key = pin_for_;
    pin_for_ = -1;
    pin_->clear();
    pin_->set_visible(false);
    for (const auto &v : tiles_->children()) {
        if (v->name == "profiles.profile." + std::to_string(key)) ui.focus(v.get());
    }
}


void ProfilesScreen::edit(Ui &ui, int key_index) {
    const profiles::Profile *p = s_.profiles.find(key_index);
    if (p == nullptr) return;
    auto d = std::make_unique<GlassDialog>(p->name);
    Dialog *dlg = d.get();
    auto add = [&](const std::string &text, const std::string &icon, std::function<void(Ui &)> fn, const std::string &id) {
        d->body().add<PillButton>(text, icon, PillButton::Style::Glass, [dlg, fn](Ui &u) {
            dlg->dismiss(u);
            fn(u);
        }, 60).name = "profiles.edit." + id;
    };
    add("Rename", "", [this, key_index](Ui &u) {
        const profiles::Profile *q = s_.profiles.find(key_index);
        ask_profile_name(u, "Rename profile", q ? q->name : "", [this, key_index](Ui &v, const std::string &name) {
            if (s_.profiles.rename(key_index, name)) save();
            rebuild();
            if (first_) v.focus(first_);
        });
    }, "rename");
    add("Colour", "", [this, key_index](Ui &u) {
        choose_profile_colour(u, s_, key_index, [this](Ui &) { rebuild(); });
    }, "colour");
    // Changing or removing a PIN asks for the current one first.
    add(p->locked() ? "Change PIN" : "Set a PIN", "lock", [this, key_index](Ui &u) {
        unlock_profile(u, s_, key_index, [this, key_index](Ui &v) {
            ask_pin(v, "New PIN", [this, key_index](Ui &w, const std::string &pin) {
                if (!s_.profiles.set_pin(key_index, pin)) {
                    w.toast("A PIN is four digits.");
                    return;
                }
                save();
                rebuild();
                w.toast("PIN set.");
            });
        });
    }, "pin");
    if (p->locked()) {
        add("Remove PIN", "", [this, key_index](Ui &u) {
            unlock_profile(u, s_, key_index, [this, key_index](Ui &v) {
                s_.profiles.set_pin(key_index, "");
                save();
                rebuild();
                v.toast("PIN removed.");
            });
        }, "unpin");
    }
    if (key_index != 0) {
        add("Delete profile", "trash", [this, key_index](Ui &u) {
            unlock_profile(u, s_, key_index, [this, key_index](Ui &v) {
                const bool was_current = s_.profiles.selected() == key_index;
                if (was_current) s_.router->switch_profile(v, 0, false);  // stay here
                s_.profiles.remove(key_index);
                save();
                s_.settings->erase_profile(key_index);
                s_.library->erase_profile(key_index);
                WNK_LOGI(kCat, "profiles: deleted %d", key_index);
                rebuild();
                if (first_) v.focus(first_);
            });
        }, "delete");
    }
    d->add_button("Close");
    ui.show_dialog(std::move(d));
}


void ProfilesScreen::ask_pin(Ui &ui, const std::string &title, std::function<void(Ui &, const std::string &)> done) {
    prompt_pin(ui, title, "Four digits, asked when this profile is chosen.", std::move(done));
}

}  // namespace csweb::ui
