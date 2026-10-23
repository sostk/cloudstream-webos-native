// SPDX-License-Identifier: GPL-3.0-or-later
#include "appswitching-control-block/AcbAPI.h"
#include "trace.hpp"

static AcbAPI_Callback saved_callback;
static long next_id = 40;
static bool in_load() { return flag("ACB_TEST_INSIDE_LOAD"); }
static void task(long *p) { assert(p); *p = 99; }
extern "C" {
long AcbAPI_create() { assert(!in_load()); trace("acb.create"); return flag("ACB_TEST_CREATE_FAIL") ? 0 : ++next_id; }
bool AcbAPI_initialize(long, AcbAPI_PlayerType type, const char *app, AcbAPI_Callback cb) {
    assert(type == PLAYER_TYPE_MSE && std::strcmp(app, "org.csweb.tv.legacy") == 0);
    saved_callback = cb;
    cb(41, 0, 0, 1, 0, "early init callback");
    trace("acb.initialize %s", app);
    return !flag("ACB_TEST_INIT_FAIL");
}
void AcbAPI_destroy(long id) {
    trace("acb.destroy %ld", id);
    saved_callback(id, 0, 0, 1, 0, "late callback");
}
#ifndef ACB_NO_OPTIONAL
int AcbAPI_finalize(long) { trace("acb.finalize"); return 0; }
int AcbAPI_setMediaAudioData(long, const char *s, long *t) {
    assert(!in_load()); task(t); trace("acb.audio %s", s); return 0;
}
int AcbAPI_setCustomDisplayWindow(long, long x, long y, long w, long h,
                                 long dx, long dy, long dw, long dh, bool full, long *t) {
    assert(!in_load()); task(t);
    trace("acb.custom %ld,%ld %ldx%ld -> %ld,%ld %ldx%ld full=%d", x, y, w, h, dx, dy, dw, dh, full);
    return 0;
}
#endif
bool AcbAPI_setSinkType(long, AcbAPI_SinkType sink) {
    assert(!in_load() && sink == SINK_TYPE_MAIN); trace("acb.sink"); return true;
}
bool AcbAPI_setMediaId(long, const char *id) {
    assert(!in_load()); trace("acb.media %s", id); return !flag("ACB_TEST_BIND_FAIL");
}
#ifndef ACB_MISSING_REQUIRED
int AcbAPI_setMediaVideoData(long, const char *s, long *t) {
    assert(!in_load()); task(t); trace("acb.video %s", s); return 0;
}
#endif
int AcbAPI_setState(long, AcbAPI_AppState app, AcbAPI_PlayState play, long *t) {
    assert(!in_load() && app == APPSTATE_FOREGROUND); task(t); trace("acb.state %d", play); return 0;
}
int AcbAPI_setDisplayWindow(long, long x, long y, long w, long h, bool full, long *t) {
    assert(!in_load()); task(t); trace("acb.display %ld,%ld %ldx%ld full=%d", x, y, w, h, full); return 0;
}
}
