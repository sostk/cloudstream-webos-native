// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/seek_bar.hpp"

#include "ui/theme/theme.hpp"

#include "wnk/ui/ui.hpp"

#include <algorithm>

namespace csweb::ui {

using namespace wnk::ui;
using player::Scrub;

namespace {
constexpr float kHeight = 28;  // room for the knob
constexpr float kTrack = 8;
constexpr float kKnob = 24;    // CloudStream's scrubber_enabled_size
}  // namespace

SeekBar::SeekBar(SeekFn on_seek, ScrubFn on_scrub) : on_seek_(std::move(on_seek)), on_scrub_(std::move(on_scrub)) {
    set_focusable(true);
    set_focus_ring(false);
    accessible_label = "Seek bar";
}

SeekBar &SeekBar::set_times(int64_t position_us, int64_t duration_us) {
    position_us_ = position_us;
    duration_us_ = duration_us;
    return *this;
}

SeekBar &SeekBar::set_active(bool on) {
    active_ = on;
    return *this;
}

int64_t SeekBar::shown_position() const { return scrub_.active() ? scrub_.target_us() : position_us_; }

wnk::Size SeekBar::measure_content(Ui &, wnk::Size available) { return {available.w, kHeight}; }

void SeekBar::commit(Ui &ui, int64_t target) {
    if (target != Scrub::kNone && on_seek_) on_seek_(ui, target);
}

void SeekBar::update(Ui &ui, float) {
    if (scrub_.active()) commit(ui, scrub_.finish(static_cast<int64_t>(ui.now_ms())));
}

bool SeekBar::on_action(Ui &ui, const wnk::input::ActionEvent &e) {
    using wnk::input::Action;
    using wnk::input::Phase;
    if (!active_ || e.phase == Phase::Release) return false;
    if (e.action == Action::Left || e.action == Action::Right) {
        if (!scrub_.step(e.action == Action::Right ? 1 : -1, position_us_, duration_us_, static_cast<int64_t>(ui.now_ms()))) {
            return false;  // live: the arrows move focus as elsewhere
        }
        if (on_scrub_) on_scrub_(ui);
        return true;
    }
    if (e.action == Action::Confirm && scrub_.active()) {
        commit(ui, scrub_.finish(static_cast<int64_t>(ui.now_ms()), /*now=*/true));
        return true;
    }
    return false;
}

void SeekBar::on_focus_changed(Ui &ui, bool focused) {
    if (!focused && scrub_.active()) commit(ui, scrub_.finish(static_cast<int64_t>(ui.now_ms()), /*now=*/true));
}

void SeekBar::draw(Ui &ui) {
    auto &r = ui.renderer();
    const wnk::Rect f = frame();
    const wnk::Rect track{f.x, f.y + (f.h - kTrack) * 0.5f, f.w, kTrack};
    r.fill_rect(track, ui.fade(theme::kSurfaceRaised), kTrack * 0.5f);
    const int64_t at = shown_position();
    const float frac = duration_us_ > 0 && at != Scrub::kNone
                           ? std::clamp(static_cast<float>(at) / static_cast<float>(duration_us_), 0.0f, 1.0f)
                           : 0.0f;
    if (frac > 0) r.fill_rect({track.x, track.y, std::max(kTrack, track.w * frac), kTrack}, ui.fade(theme::kAccent), kTrack * 0.5f);
    if (ui.is_focused(this)) {
        const float cx = track.x + track.w * frac;
        r.fill_rect({cx - kKnob * 0.5f, f.y + (f.h - kKnob) * 0.5f, kKnob, kKnob}, ui.fade(theme::kFocusFill), kKnob * 0.5f);
    }
}

}  // namespace csweb::ui
