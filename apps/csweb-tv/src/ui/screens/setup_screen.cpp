// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/setup_screen.hpp"

#include "profiles/profiles.hpp"
#include "ui/screens/profiles_screen.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/glass_dialog.hpp"
#include "ui/widgets/icons.hpp"
#include "ui/widgets/pill_button.hpp"
#include "ui/widgets/profile_avatar.hpp"

#include "wnk/log.hpp"

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr const char *kCat = "csweb";
const char *const kStepNames[] = {"Welcome", "Who's watching", "Ready"};
constexpr int kSteps = 3;

// The numbered steps across the top (design 14): done in the accent colour, the current one white.
class StepIndicator : public View {
 public:
    explicit StepIndicator(const int &step) : step_(step) {}

 protected:
    wnk::Size measure_content(Ui &ui, wnk::Size) override {
        float w = 0;
        const wnk::render::TextStyle s{ui.theme().bold, 24, theme::kText};
        for (int i = 0; i < kSteps; i++) w += 44 + 14 + ui.renderer().measure_text(s, kStepNames[i]) + (i + 1 < kSteps ? 92 : 0);
        return {w, 44};
    }
    void draw(Ui &ui) override {
        auto &r = ui.renderer();
        const wnk::Rect f = frame();
        float x = f.x;
        for (int i = 0; i < kSteps; i++) {
            const bool done = i < step_, current = i == step_;
            const theme::Color fill = done ? theme::kAccent : current ? theme::kFocusFill : theme::Color::hex(0xFFFFFF14);
            r.fill_rect({x, f.y, 44, 44}, ui.fade(fill), 22);
            const std::string n = std::to_string(i + 1);
            const wnk::render::TextStyle ns{ui.theme().bold, 22, ui.fade(done || current ? theme::kOnFocus : theme::kTextMuted)};
            const float nw = r.measure_text(ns, n);
            r.draw_text(ns, {x + 22 - nw * 0.5f, f.y + (44 - r.line_height(ns)) * 0.5f + r.ascent(ns)}, n);
            x += 44 + 14;
            const wnk::render::TextStyle ls{ui.theme().bold, 24, ui.fade(current ? theme::kText : theme::kTextMuted)};
            x += r.draw_text(ls, {x, f.y + (44 - r.line_height(ls)) * 0.5f + r.ascent(ls)}, kStepNames[i]);
            if (i + 1 < kSteps) {
                r.fill_rect({x + 16, f.y + 21, 60, 2}, ui.fade(theme::Color::hex(0xFFFFFF2E)));
                x += 92;
            }
        }
    }

 private:
    const int &step_;
};

// A soft card on the right holding the brand (welcome and last step).
void brand_card(Column &side) {
    side.set_background(theme::Color::hex(0xFFFFFF0D), 30);
    side.set_padding(Insets::all(56));
    auto &logo = side.add<ImageView>(asset_url("brand/logo.png"));
    logo.set_fit(ImageFit::Contain);
    logo.set_size(380, 180);
    logo.set_align(Align::Center, Align::Start);
    auto &name = side.add<Label>("CloudStream", TextRole::Title);
    name.set_bold(true).set_font_size(theme::kTitle);
    name.set_align(Align::Center, Align::Start);
    auto &tag = side.add<Label>("Native app for LG webOS", TextRole::Small);
    tag.set_color(theme::kTextSecondary).set_font_size(theme::kLabel);
    tag.set_align(Align::Center, Align::Start);
}
}  // namespace

SetupScreen::SetupScreen(app::Services &s, std::function<void(Ui &, bool)> done)
    : Screen("Setup"), s_(s), done_(std::move(done)) {
    name = "setup";
    set_background(theme::kBackground);
    auto &page = add<Column>(0);
    page.set_width(Dim::fill()).set_height(Dim::fill());
    page.set_padding({theme::kSafeX, 72, theme::kSafeX, 72});
    steps_ = &page.add<wnk::ui::Row>(0);
    steps_->add<StepIndicator>(step_);
    auto &body = page.add<wnk::ui::Row>(110);
    body.set_width(Dim::fill()).set_height(Dim::fill());
    body.set_padding({0, 130, 0, 0});
    main_ = &body.add<Column>(28);
    main_->set_width(Dim::fixed(1000));
    side_ = &body.add<Column>(26);
    side_->set_width(Dim::fixed(560));
    side_->set_align(Align::Start, Align::Start);
    show_step(nullptr, 0);
}

void SetupScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (first_) ui.focus(first_);
}

bool SetupScreen::on_back(Ui &ui) {
    if (step_ > 0) {
        show_step(&ui, step_ - 1);
        return true;
    }
    if (s_.ctx) s_.ctx->quit("back on setup");  // the setup is the first screen
    return true;
}

void SetupScreen::show_step(Ui *ui, int step) {
    step_ = step;
    main_->clear_children();
    side_->clear_children();
    side_->set_background(theme::Color::hex(0x00000000));
    side_->set_padding({});
    first_ = nullptr;
    if (step == 0) build_welcome();
    else if (step == 1) build_profiles();
    else build_finish();
    if (ui && first_) ui->focus(first_);
}

void SetupScreen::build_welcome() {
    main_->add<Label>("Welcome to CloudStream", TextRole::Title).set_font_size(72).set_bold(true).set_max_lines(2);
    main_->add<Label>("This app comes with no content of its own. Everything you browse comes from extensions in "
                      "repositories you choose to add. Only add sources you trust and are allowed to use.")
        .set_color(theme::kTextSecondary)
        .set_font_size(theme::kBody)
        .set_max_lines(5);
    main_->add<Label>("Next, add the people who watch on this TV.")
        .set_color(theme::kTextMuted)
        .set_font_size(theme::kLabel);
    auto &buttons = main_->add<wnk::ui::Row>(16);
    buttons.set_padding({0, 18, 0, 0});
    auto &go = buttons.add<PillButton>("Get started", "chevron-right", PillButton::Style::Primary,
                                       [this](Ui &ui) { show_step(&ui, 1); }, 68);
    go.name = "setup.start";
    first_ = &go;
    brand_card(*side_);
}

void SetupScreen::build_profiles() {
    main_->add<Label>("Who's watching?", TextRole::Title).set_font_size(72).set_bold(true);
    main_->add<Label>("Add everyone who watches on this TV. Each profile keeps its own library, continue watching, "
                      "home source and searches. Choose a profile to rename it or change its colour.")
        .set_color(theme::kTextSecondary)
        .set_font_size(theme::kBody)
        .set_max_lines(4);
    auto &row = main_->add<wnk::ui::Row>(40);
    row.set_padding({0, 20, 0, 0});
    row.set_focus_scope();
    View *focus = nullptr;
    for (const auto &p : s_.profiles.all()) {
        const int key = p.key_index;
        auto &a = row.add<ProfileAvatar>(p.name, profiles::initials(p.name), p.image_index,
                                         [this, key](Ui &ui) { edit_profile(ui, key); }, 160);
        a.name = "setup.profile." + std::to_string(key);
        if (focus == nullptr || key == focus_key_) focus = &a;
    }
    if (s_.profiles.all().size() < 8) {
        auto &add = row.add<ProfileAvatar>("Add", "+", -1, [this](Ui &ui) { add_profile(ui); }, 160);
        add.name = "setup.add";
        add.accessible_label = "Add profile";
    }
    auto &buttons = main_->add<wnk::ui::Row>(16);
    buttons.set_padding({0, 10, 0, 0});
    auto &next = buttons.add<PillButton>("Continue", "chevron-right", PillButton::Style::Primary,
                                         [this](Ui &ui) { show_step(&ui, 2); }, 68);
    next.name = "setup.continue";
    buttons.add<PillButton>("Back", "back", PillButton::Style::Glass, [this](Ui &ui) { show_step(&ui, 0); }, 68).name =
        "setup.back";
    first_ = focus_key_ >= 0 && focus ? focus : &next;
    focus_key_ = -1;
}

void SetupScreen::build_finish() {
    main_->add<Label>("You're all set", TextRole::Title).set_font_size(72).set_bold(true);
    main_->add<Label>("To start browsing, add a repository of extensions and install the ones you want. You can do it "
                      "now, or later from Settings > Extensions.")
        .set_color(theme::kTextSecondary)
        .set_font_size(theme::kBody)
        .set_max_lines(4);
    const size_t n = s_.profiles.all().size();
    if (n > 1) {
        main_->add<Label>("When the app starts, it will ask who's watching.")
            .set_color(theme::kTextMuted)
            .set_font_size(theme::kLabel);
    }
    auto &buttons = main_->add<wnk::ui::Row>(16);
    buttons.set_padding({0, 18, 0, 0});
    auto &ext = buttons.add<PillButton>("Open extensions", "extensions", PillButton::Style::Primary,
                                        [this](Ui &ui) { finish(ui, true); }, 68);
    ext.name = "setup.extensions";
    buttons.add<PillButton>("Go to Home", "", PillButton::Style::Glass, [this](Ui &ui) { finish(ui, false); }, 68).name =
        "setup.home";
    first_ = &ext;
    brand_card(*side_);
}

void SetupScreen::edit_profile(Ui &ui, int key_index) {
    const profiles::Profile *p = s_.profiles.find(key_index);
    if (p == nullptr) return;
    auto d = std::make_unique<GlassDialog>(p->name);
    Dialog *dlg = d.get();
    auto add = [&](const std::string &text, const std::string &icon, std::function<void(Ui &)> fn, const std::string &id) {
        d->body().add<PillButton>(text, icon, PillButton::Style::Glass, [dlg, fn](Ui &u) {
            dlg->dismiss(u);
            fn(u);
        }, 60).name = "setup.edit." + id;
    };
    add("Rename", "", [this, key_index](Ui &u) {
        const profiles::Profile *q = s_.profiles.find(key_index);
        ask_profile_name(u, "Rename profile", q ? q->name : "", [this, key_index](Ui &v, const std::string &name) {
            if (s_.profiles.rename(key_index, name)) {
                s_.settings->set(settings::kProfiles, s_.profiles.to_json());
                s_.profile_changed.emit();
            }
            focus_key_ = key_index;
            show_step(&v, 1);
        });
    }, "rename");
    add("Colour", "", [this, key_index](Ui &u) {
        choose_profile_colour(u, s_, key_index, [this, key_index](Ui &v) {
            focus_key_ = key_index;
            show_step(&v, 1);
        });
    }, "colour");
    if (key_index != 0) {
        add("Remove", "trash", [this, key_index](Ui &u) {
            if (s_.profiles.selected() == key_index) s_.router->switch_profile(u, 0, false);
            s_.profiles.remove(key_index);
            s_.settings->set(settings::kProfiles, s_.profiles.to_json());
            s_.settings->erase_profile(key_index);
            s_.library->erase_profile(key_index);
            show_step(&u, 1);
        }, "remove");
    }
    d->add_button("Close");
    ui.show_dialog(std::move(d));
}

void SetupScreen::add_profile(Ui &ui) {
    ask_profile_name(ui, "New profile", "", [this](Ui &u, const std::string &name) {
        if (name.empty()) return;
        const int key = s_.profiles.add(name).key_index;
        s_.settings->set(settings::kProfiles, s_.profiles.to_json());
        WNK_LOGI(kCat, "setup: added profile %d", key);
        focus_key_ = key;
        show_step(&u, 1);
    });
}

void SetupScreen::finish(Ui &ui, bool open_extensions) {
    s_.settings->set(settings::kSetupDone, true);
    WNK_LOGI(kCat, "setup: done (%zu profiles)", s_.profiles.all().size());
    if (auto done = std::move(done_)) {
        done_ = nullptr;
        done(ui, open_extensions);
    }
}

}  // namespace csweb::ui
