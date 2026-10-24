// SPDX-License-Identifier: GPL-3.0-or-later
#include "trace.hpp"

using Callback = void (*)(int, long long, const char *, void *);
struct Object { Callback callback; void *context; };
static Callback late_callback;
static void *late_context;
static Object *last_object;
extern "C" {
void starfish_test_late_event() {
    last_object->callback(4, 0, "{\"video\":\"STALE\"}", last_object->context);
}
void ctor(void *, const char *) __asm__("_ZN17StarfishMediaAPIsC1EPKc");
void dtor(void *) __asm__("_ZN17StarfishMediaAPIsD1Ev");
int load(void *, const char *, Callback, void *) __asm__("_ZN17StarfishMediaAPIs4LoadEPKcPFvixS1_PvES2_");
void feed(void *, void *, const char *) __asm__("_ZN17StarfishMediaAPIs4FeedB5cxx11EPKc");
int play(void *) __asm__("_ZN17StarfishMediaAPIs4PlayEv");
int pause(void *) __asm__("_ZN17StarfishMediaAPIs5PauseEv");
int unload(void *) __asm__("_ZN17StarfishMediaAPIs6UnloadEv");
int eos(void *) __asm__("_ZN17StarfishMediaAPIs7pushEOSEv");
void foreground(void *) __asm__("_ZN17StarfishMediaAPIs16notifyForegroundEv");
#ifndef SF_NO_MEDIA_ID
const char *media_id(void *) __asm__("_ZN17StarfishMediaAPIs10getMediaIDEv");
const char *media_id(void *) {
    assert(!flag("ACB_TEST_INSIDE_LOAD"));
    return flag("ACB_TEST_EMPTY_MEDIA") ? "" : "test-media-id";
}
#endif
void ctor(void *p, const char *) { *static_cast<Object *>(p) = {}; trace("sf.ctor"); }
void dtor(void *) { trace("sf.dtor"); }
void metadata(Object *p) {
    p->callback(4, 0, "{\"video\":{\"width\":1280,\"height\":720}}", p->context);
    p->callback(7, 0, "{\"audio\":{\"channels\":2}}", p->context);
    p->callback(22, 0, "{\"loadCompleted\":true}", p->context);
}
int load(void *p, const char *payload, Callback cb, void *ctx) {
    auto *o = static_cast<Object *>(p);
    o->callback = cb; o->context = ctx;
    last_object = o;
    if (late_callback) late_callback(4, 0, "{\"video\":\"STALE\"}", late_context);
    setenv("ACB_TEST_INSIDE_LOAD", "1", 1);
    trace("sf.load %s", payload);
    if (!flag("ACB_TEST_DELAYED")) metadata(o);
    unsetenv("ACB_TEST_INSIDE_LOAD");
    trace("sf.load.return");
    return !flag("ACB_TEST_LOAD_FAIL");
}
void foreground(void *) { trace("sf.foreground"); }
int play(void *p) {
    auto *o = static_cast<Object *>(p);
    trace("sf.play");
    if (flag("ACB_TEST_DELAYED")) { unsetenv("ACB_TEST_DELAYED"); metadata(o); }
    o->callback(26, 0, "{\"playing\":true}", o->context); return 1;
}
int pause(void *p) {
    auto *o = static_cast<Object *>(p); trace("sf.pause");
    o->callback(27, 0, "{\"paused\":true}", o->context); return 1;
}
int unload(void *p) {
    auto *o = static_cast<Object *>(p); trace("sf.unload");
    late_callback = o->callback; late_context = o->context;
    o->callback(23, 0, "{\"unloadCompleted\":true}", o->context); return 1;
}
void feed(void *reply, void *, const char *) { *static_cast<const char **>(reply) = "Ok"; trace("sf.feed"); }
int eos(void *) { return 1; }
}
