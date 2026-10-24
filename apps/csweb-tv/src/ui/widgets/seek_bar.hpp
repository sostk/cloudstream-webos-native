// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's time bar. It takes focus like CloudStream's (Media3's DefaultTimeBar): Left/Right
// scrub (player/scrub.hpp), the bar and the time follow the target, and the seek goes out when the
// keys stop, on OK, or when focus leaves. Focus shows as the scrubber knob, not the kit's ring.
#pragma once

#include "player/scrub.hpp"

#include "wnk/ui/view.hpp"

#include <cstdint>
#include <functional>

namespace csweb::ui {

class SeekBar : public wnk::ui::View {
 public:
    using SeekFn = std::function<void(wnk::ui::Ui &, int64_t pts_us)>;
    using ScrubFn = std::function<void(wnk::ui::Ui &)>;  // every press (keeps the controls up)
    explicit SeekBar(SeekFn on_seek, ScrubFn on_scrub = {});

    // Where playback stands; the bar shows it unless scrubbing.
    SeekBar &set_times(int64_t position_us, int64_t duration_us);
    // Off while the controls are hidden: the arrows then belong to the screen (seek without the bar).
    SeekBar &set_active(bool on);
    // The scrub target while scrubbing, else the position.
    int64_t shown_position() const;
    bool scrubbing() const { return scrub_.active(); }

 protected:
    wnk::Size measure_content(wnk::ui::Ui &ui, wnk::Size available) override;
    void update(wnk::ui::Ui &ui, float dt) override;
    void draw(wnk::ui::Ui &ui) override;
    bool on_action(wnk::ui::Ui &ui, const wnk::input::ActionEvent &e) override;
    void on_focus_changed(wnk::ui::Ui &ui, bool focused) override;

 private:
    void commit(wnk::ui::Ui &ui, int64_t target);
    SeekFn on_seek_;
    ScrubFn on_scrub_;
    player::Scrub scrub_;
    int64_t position_us_ = player::Scrub::kNone;
    int64_t duration_us_ = player::Scrub::kNone;
    bool active_ = true;
};

}  // namespace csweb::ui
