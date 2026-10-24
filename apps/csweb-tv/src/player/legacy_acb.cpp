// SPDX-License-Identifier: GPL-3.0-or-later
// ACB call sequence: Kodi MediaPipelineWebOS/RendererStarfish and Moonlight SS4S
// smp_resource_acb.c. Stable declarations use the webOS NDK header; metadata uses
// the modern three-argument ABI even with an older SDK (see MediaDataFn below).
// All entry points are dlsym'd so newer firmware need not provide libAcbAPI.
#include "player/legacy_acb.hpp"

#include "wnk/log.hpp"

#include <appswitching-control-block/AcbAPI.h>
#include <dlfcn.h>

#include <mutex>
#include <utility>

namespace csweb::player {
namespace {
constexpr const char *kCat = "legacy-acb";
// Old NDK headers declare setMediaVideoData(long, const char*) and omit audio.
// webOS 4.x requires a real taskId out-pointer: the two-argument call can corrupt
// memory. This is the modern AcbAPI.h / Kodi / PlxNative ABI; SS4S likewise casts
// around old headers. Do not derive these two types from an older SDK declaration.
using MediaDataFn = int (*)(long, const char *, long *);

struct Api {
    void *handle = nullptr;
    decltype(&AcbAPI_create) create = nullptr;
    decltype(&AcbAPI_initialize) initialize = nullptr;
    decltype(&AcbAPI_destroy) destroy = nullptr;
    decltype(&AcbAPI_finalize) finalize = nullptr;  // optional; destroy is required
    decltype(&AcbAPI_setSinkType) sink = nullptr;
    decltype(&AcbAPI_setMediaId) media_id = nullptr;
    MediaDataFn video = nullptr;
    MediaDataFn audio = nullptr;  // absent on some older firmware
    decltype(&AcbAPI_setState) state = nullptr;
    decltype(&AcbAPI_setDisplayWindow) display = nullptr;
    decltype(&AcbAPI_setCustomDisplayWindow) custom_display = nullptr;  // optional crop/scale path
    std::string why;

    template <typename F>
    bool required(F &fn, const char *name) {
        fn = reinterpret_cast<F>(dlsym(handle, name));
        if (!fn && why.empty()) why = std::string("missing ") + name;
        return fn != nullptr;
    }

    Api() {
        handle = dlopen("libAcbAPI.so.1", RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            const char *reason = dlerror();
            why = reason ? reason : "libAcbAPI.so.1 unavailable";
            return;
        }
        bool ok = required(create, "AcbAPI_create");
        ok = required(initialize, "AcbAPI_initialize") && ok;
        ok = required(destroy, "AcbAPI_destroy") && ok;
        ok = required(sink, "AcbAPI_setSinkType") && ok;
        ok = required(media_id, "AcbAPI_setMediaId") && ok;
        ok = required(video, "AcbAPI_setMediaVideoData") && ok;
        ok = required(state, "AcbAPI_setState") && ok;
        ok = required(display, "AcbAPI_setDisplayWindow") && ok;
        audio = reinterpret_cast<decltype(audio)>(dlsym(handle, "AcbAPI_setMediaAudioData"));
        finalize = reinterpret_cast<decltype(finalize)>(dlsym(handle, "AcbAPI_finalize"));
        custom_display = reinterpret_cast<decltype(custom_display)>(dlsym(handle, "AcbAPI_setCustomDisplayWindow"));
        if (!ok) {
            dlclose(handle);
            handle = nullptr;
        }
        // Keep a usable library loaded for process lifetime: firmware callbacks can be late.
    }
};

const Api &api() {
    static const Api instance;
    return instance;
}

// No backend pointer: callbacks remain harmless after an ACB handle is destroyed.
void callback(long id, long task, long event, long app, long play, const char *) {
    WNK_LOGD(kCat, "callback id=%ld task=%ld event=%ld app=%ld play=%ld", id, task, event, app, play);
}
}  // namespace

struct LegacyAcb::Impl {
    long id = 0, task = 0;  // playback-thread owned; task MUST be a real out-pointer
    bool bound = false;
    std::mutex mutex;
    bool active = false, loaded = false, dirty_area = true;
    int state = -1;
    std::string video, audio;
    wnk::Rect area{0, 0, 1920, 1080};
    int video_width = 0, video_height = 0;
};

LegacyAcb::LegacyAcb() : impl_(std::make_unique<Impl>()) {}
LegacyAcb::~LegacyAcb() { close(); }

bool LegacyAcb::open(const std::string &app_id, std::string *error) {
    close();
    const auto &a = api();
    if (app_id.empty() || !a.handle) {
        *error = "legacy video plane: " + (app_id.empty() ? std::string("missing app id") : a.why);
        WNK_LOGW(kCat, "%s", error->c_str());
        return false;
    }
    auto &p = *impl_;
    p.id = a.create();
    if (!p.id || !a.initialize(p.id, PLAYER_TYPE_MSE, app_id.c_str(), &callback)) {
        *error = "legacy video plane: ACB create/initialize failed (check firmware media permissions)";
        WNK_LOGW(kCat, "%s", error->c_str());
        if (p.id) a.destroy(p.id);
        p.id = 0;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.active = true;
        p.loaded = false;
        p.state = -1;
        p.video.clear();
        p.audio.clear();
        p.dirty_area = true;
    }
    WNK_LOGI(kCat, "display path ACB: initialized id=%ld app=%s", p.id, app_id.c_str());
    return true;
}

void LegacyAcb::event(int type, const char *payload) {
    auto &p = *impl_;
    std::lock_guard<std::mutex> lock(p.mutex);
    if (!p.active) return;
    // These event numbers are shared by webOS 4 and 5+ (only numbers >0x1c shifted).
    // Forward the firmware's video/audio envelopes verbatim; never manufacture metadata.
    if (type == 4 && payload) p.video = payload;
    if (type == 7 && payload) p.audio = payload;
    if (type == 22) p.loaded = true;
    if (type == 26) p.state = PLAYSTATE_PLAYING;
    if (type == 27) p.state = PLAYSTATE_PAUSED;
}

void LegacyAcb::transport(bool paused) {
    auto &p = *impl_;
    std::lock_guard<std::mutex> lock(p.mutex);
    if (p.active) p.state = paused ? PLAYSTATE_PAUSED : PLAYSTATE_PLAYING;
}

void LegacyAcb::set_area(const wnk::Rect &area, int video_width, int video_height) {
    auto &p = *impl_;
    std::lock_guard<std::mutex> lock(p.mutex);
    if (p.area == area && p.video_width == video_width && p.video_height == video_height) return;
    p.area = area;
    p.video_width = video_width;
    p.video_height = video_height;
    p.dirty_area = true;
}

bool LegacyAcb::pump(const char *media_id, std::string *error) {
    auto &p = *impl_;
    if (!p.id) return true;  // exported-window path
    std::string video, audio;
    wnk::Rect area;
    bool dirty;
    int state, video_width, video_height;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        if (!p.loaded) return true;
        video.swap(p.video);
        audio.swap(p.audio);
        area = p.area;
        video_width = p.video_width;
        video_height = p.video_height;
        dirty = std::exchange(p.dirty_area, false);
        state = std::exchange(p.state, -1);
    }
    const auto &a = api();
    // Do not hold the mailbox mutex while calling firmware: calls may trigger callbacks.
    if (!p.bound) {
        if (!media_id || !*media_id || !a.sink(p.id, SINK_TYPE_MAIN) || !a.media_id(p.id, media_id)) {
            *error = "legacy video plane: ACB media binding failed";
            WNK_LOGW(kCat, "%s", error->c_str());
            return false;
        }
        WNK_LOGI(kCat, "bind id=%ld media=%s", p.id, media_id);
        WNK_LOGI(kCat, "state LOADED rv=%d", a.state(p.id, APPSTATE_FOREGROUND, PLAYSTATE_LOADED, &p.task));
        p.bound = true;
    }
    if (!video.empty()) WNK_LOGI(kCat, "video metadata rv=%d", a.video(p.id, video.c_str(), &p.task));
    if (!audio.empty() && a.audio) WNK_LOGI(kCat, "audio metadata rv=%d", a.audio(p.id, audio.c_str(), &p.task));
    if (dirty && area.w > 0 && area.h > 0) {
        const bool full_screen = area == wnk::Rect{0, 0, 1920, 1080};
        const int rc = a.custom_display && video_width > 0 && video_height > 0
            ? a.custom_display(p.id, 0, 0, video_width, video_height,
                               static_cast<long>(area.x), static_cast<long>(area.y),
                               static_cast<long>(area.w), static_cast<long>(area.h), false, &p.task)
            : a.display(p.id, static_cast<long>(area.x), static_cast<long>(area.y),
                        static_cast<long>(area.w), static_cast<long>(area.h), full_screen, &p.task);
        WNK_LOGI(kCat, "display %g,%g %gx%g rv=%d", static_cast<double>(area.x), static_cast<double>(area.y),
                 static_cast<double>(area.w), static_cast<double>(area.h), rc);
    }
    if (state >= 0) WNK_LOGI(kCat, "state %d rv=%d", state,
                             a.state(p.id, APPSTATE_FOREGROUND, static_cast<AcbAPI_PlayState>(state), &p.task));
    return true;
}

void LegacyAcb::close() {
    auto &p = *impl_;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.active = false;
        p.loaded = false;
        p.video.clear();
        p.audio.clear();
        p.state = -1;
    }
    if (!p.id) return;
    const auto &a = api();
    if (p.bound) WNK_LOGI(kCat, "state UNLOADED rv=%d", a.state(p.id, APPSTATE_FOREGROUND, PLAYSTATE_UNLOADED, &p.task));
    if (a.finalize) WNK_LOGI(kCat, "finalize rv=%d", a.finalize(p.id));
    a.destroy(p.id);
    WNK_LOGI(kCat, "destroyed id=%ld", p.id);
    p.id = p.task = 0;
    p.bound = false;
}

}  // namespace csweb::player
