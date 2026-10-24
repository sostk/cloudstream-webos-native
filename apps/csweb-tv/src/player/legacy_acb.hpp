// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wnk/geometry.hpp"

#include <memory>
#include <string>

namespace csweb::player {

// Old webOS video-plane control. Firmware callbacks only enqueue metadata/state;
// pump(), open() and close() run on the playback thread, after Starfish Load returns.
// set_area() may run on the UI thread. No LG library is linked or bundled.
class LegacyAcb {
 public:
    LegacyAcb();
    ~LegacyAcb();
    LegacyAcb(const LegacyAcb &) = delete;
    LegacyAcb &operator=(const LegacyAcb &) = delete;

    bool open(const std::string &app_id, std::string *error);
    void event(int type, const char *payload);
    bool pump(const char *media_id, std::string *error);
    void set_area(const wnk::Rect &area, int video_width = 0, int video_height = 0);
    void transport(bool paused);
    void close();

 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace csweb::player
