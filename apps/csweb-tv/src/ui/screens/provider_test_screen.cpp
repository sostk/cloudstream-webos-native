// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/provider_test_screen.hpp"

#include "ui/theme/theme.hpp"
#include "ui/widgets/glass_dialog.hpp"
#include "ui/widgets/pill_button.hpp"

#include "wnk/log.hpp"
#include "wnk/ui/forms.hpp"

#include <algorithm>
#include <random>

namespace csweb::ui {

using namespace wnk::ui;

namespace {
constexpr const char *kCat = "csweb";
}

ProviderTestScreen::ProviderTestScreen(app::Services &s) : Screen("Test extensions"), s_(s) {
    name = "provider_tests";
    auto &scroll = add<ScrollView>();
    scroll.margin = 160;
    auto &col = scroll.add<Column>(22);
    col.set_padding({theme::kSafeX, 120, theme::kSafeX, 80});
    col.add<Label>("Test extensions", TextRole::Title).set_font_size(theme::kDisplay).set_bold(true);
    auto &intro = col.add<Label>("Each source loads its home page, searches, opens a result and loads its links, as CloudStream's "
                                 "provider test does. This uses the network and can take a minute.");
    intro.set_muted(true).set_max_lines(2);
    intro.set_width(Dim::fixed(1400));
    auto &bar = col.add<wnk::ui::Row>(24);
    start_ = &bar.add<PillButton>("Start", "play", PillButton::Style::Primary, [this](Ui &) {
        if (started_ && std::any_of(tests_.begin(), tests_.end(), [](const auto &t) {
                return t->state() == runtime::ProviderTest::State::Running;
            })) {
            stop();
        } else {
            start();
        }
    }, 64);
    start_->name = "tests.start";
    summary_ = &bar.add<Label>("");
    summary_->set_align(Align::Start, Align::Center);
    list_ = &col.add<Column>(10);
    list_->set_width(Dim::fixed(1300));
}

ProviderTestScreen::~ProviderTestScreen() { tests_.clear(); }

void ProviderTestScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (!started_) ui.focus(start_);
    refresh();
}

void ProviderTestScreen::start() {
    started_ = true;
    reported_ = false;
    tests_.clear();
    rows_.clear();
    list_->clear_children();
    std::random_device rd;
    for (const auto &p : s_.plugins->providers()) {
        auto t = std::make_unique<runtime::ProviderTest>(
            p,
            [this](nlohmann::json req, runtime::RuntimeClient::EventHandler ev, runtime::RuntimeClient::DoneHandler done) {
                return s_.runtime->request(std::move(req), std::move(ev), std::move(done));
            },
            [this](runtime::RuntimeClient::RequestId id) { s_.runtime->cancel(id); }, [this] { refresh(); }, rd());
        const size_t index = tests_.size();
        auto &row = list_->add<ValueRow>(p.name, "", [this, index](Ui &ui) { show_log(ui, index); });
        row.name = "tests.provider." + std::to_string(index);
        rows_.push_back(&row);
        tests_.push_back(std::move(t));
    }
    for (auto &t : tests_) t->start();
    WNK_LOGI(kCat, "provider tests: %zu providers", tests_.size());
    refresh();
}

void ProviderTestScreen::stop() {
    for (auto &t : tests_) t->stop();
    refresh();
}

void ProviderTestScreen::refresh() {
    size_t passed = 0, failed = 0, running = 0;
    for (size_t i = 0; i < tests_.size(); i++) {
        const auto &t = *tests_[i];
        std::string value = runtime::test_state_name(t.state());
        if (t.state() == runtime::ProviderTest::State::Running && !t.step().empty()) value += " · " + t.step();
        if (i < rows_.size()) rows_[i]->set_value(value);
        passed += t.state() == runtime::ProviderTest::State::Passed;
        failed += t.state() == runtime::ProviderTest::State::Failed;
        running += t.state() == runtime::ProviderTest::State::Running;
    }
    if (!started_) {
        summary_->set_text(s_.plugins->providers().empty() ? "No sources are loaded."
                                                           : std::to_string(s_.plugins->providers().size()) + " sources loaded");
    } else {
        summary_->set_text("Passed " + std::to_string(passed) + "   Failed " + std::to_string(failed) + "   Total " +
                           std::to_string(tests_.size()));
    }
    start_->set_text(running > 0 ? "Stop" : started_ ? "Start again" : "Start");
    start_->set_icon(running > 0 ? "pause" : "play");
    if (started_ && running == 0 && !tests_.empty() && !reported_) {
        reported_ = true;
        for (const auto &t : tests_)
            WNK_LOGI(kCat, "provider test %s: %s", t->provider().name.c_str(), runtime::test_state_name(t->state()));
    }
}

void ProviderTestScreen::show_log(Ui &ui, size_t index) {
    if (index >= tests_.size()) return;
    const auto &t = *tests_[index];
    std::string text;
    for (const auto &l : t.log()) text += std::string(1, l.level) + "  " + l.text + "\n";
    auto d = std::make_unique<GlassDialog>(t.provider().name + " · " + runtime::test_state_name(t.state()));
    auto &view = d->body().add<TextView>(text.empty() ? "Not started." : text, TextRole::Small);
    view.set_width(Dim::fixed(GlassDialog::kContentWidth)).set_height(Dim::fixed(520));
    view.name = "tests.log";
    d->add_button("Close");
    ui.show_dialog(std::move(d));
}

}  // namespace csweb::ui
