// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/legacy_starfish_backend.hpp"
#include "wnk/log.hpp"
#include "trace.hpp"

namespace wnk::log {
void logf(Level, const char *, const char *, ...) {}  // no process logger in this harness
}

#ifndef NO_SDL_EXPORTS
extern "C" {
const char *SDL_webOSCreateExportedWindow(int) {
    trace("sdl.create"); return flag("ACB_TEST_EXPORTED") ? "exported-id" : nullptr;
}
int SDL_webOSSetExportedWindow(const char *id, void *, void *) {
    assert(std::strcmp(id, "exported-id") == 0); trace("sdl.place"); return 1;
}
void SDL_webOSDestroyExportedWindow(const char *) { trace("sdl.destroy"); }
}
#endif

int main(int argc, char **argv) {
    assert(argc == 2);
    const bool expect_failure = std::strcmp(argv[1], "failure") == 0;
    using namespace wnk::media;
    std::string error;
    assert(LegacyStarfishBackend::available(&error));
    LegacyStarfishOptions options;
    options.app_id = flag("ACB_TEST_NO_APP_ID") ? "" : "org.csweb.tv.legacy";
    LegacyStarfishBackend backend(options);
    StreamFormat format;
    VideoFormat video;
    video.codec = CodecId::H264; video.width = 1280; video.height = 720; video.frame_rate = 24;
    format.video = video;
    for (int iteration = 0; iteration < 2; ++iteration) {
        if (!backend.load(format, &error)) {
            assert(expect_failure && !error.empty());
            trace("expected.error %s", error.c_str());
            return 0;
        }
        assert(!expect_failure);
        Packet packet;
        packet.type = TrackType::Video; packet.pts_us = 0;
        packet.data = {0, 0, 0, 1, 9};
        for (int i = 0; i < 3; ++i) assert(backend.feed(packet) == MediaBackend::Feed::Accepted);
        backend.set_area({100, 50, 960, 540});
        assert(backend.feed(packet) == MediaBackend::Feed::Accepted);
        assert(backend.pause(true));
        assert(backend.pause(false));
        assert(backend.flush(&error));
        backend.unload();
    }
}
