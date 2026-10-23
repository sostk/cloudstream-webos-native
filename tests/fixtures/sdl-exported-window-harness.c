// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int SDL_webOSExportedWindowType;
typedef struct webos_foreign_window {
    void *exported;
    char window_id[32];
    struct webos_foreign_window *next;
} webos_foreign_window;
struct foreign_table { int count; webos_foreign_window *windows; };
typedef struct { struct foreign_table *webos_foreign_table; void *webos_foreign; void *display; } SDL_VideoData;
typedef struct { void *surface; } SDL_WindowData;
typedef struct SDL_Window { void *driverdata; unsigned flags; struct SDL_Window *next; } SDL_Window;
typedef struct { void *driverdata; SDL_Window *windows; void *webos_foreign_lock; } SDL_VideoDevice;
#define _THIS SDL_VideoDevice *_this
#define SDL_WINDOW_OPENGL 2
#define SDL_WEBOS_EXPORTED_WINDOW_TYPE_OPAQUE 2
#define SDL_LOG_CATEGORY_VIDEO 0
static char error_text[256];
static int allocations, lock_depth, exports;
static int exported_listener;
static void SDL_SetError(const char *format, ...) {
    va_list args; va_start(args, format); vsnprintf(error_text, sizeof error_text, format, args); va_end(args);
}
static SDL_Window *SDL_GL_GetCurrentWindow(void) { return NULL; }
static int SDL_RecreateWindow(SDL_Window *window, unsigned flags) { window->flags=flags; return 0; }
static void SDL_LockMutex(void *p) { (void)p; ++lock_depth; }
static void SDL_UnlockMutex(void *p) { (void)p; --lock_depth; }
static void *SDL_calloc(size_t n, size_t s) { ++allocations; return calloc(n,s); }
static void *wl_webos_foreign_export_element(void *foreign, void *surface, int type) {
    assert(foreign && surface); (void)type; ++exports; return (void *)1;
}
static void wl_webos_exported_add_listener(void *exported, void *listener, webos_foreign_window *window) {
    (void)listener; assert(exported); strcpy(window->window_id, "test-video-window");
}
static void SDL_Delay(int n) { (void)n; abort(); }
static void WAYLAND_wl_display_dispatch(void *p) { (void)p; abort(); }
static void SDL_LogInfo(int category, const char *format, ...) { (void)category; (void)format; }
/* FUNCTION */
int main(int argc, char **argv) {
    assert(argc==2);
    SDL_WindowData wd = {(void *)1};
    SDL_Window window = {&wd, SDL_WINDOW_OPENGL, NULL};
    struct foreign_table table = {0, NULL};
    SDL_VideoData data = {&table, (void *)1, (void *)1};
    SDL_VideoDevice dev = {&data, &window, (void *)1};
    int success = strcmp(argv[1], "available")==0;
    if (strcmp(argv[1], "missing-table")==0) data.webos_foreign_table=NULL;
    else if (strcmp(argv[1], "missing-protocol")==0) data.webos_foreign=NULL;
    else if (strcmp(argv[1], "missing-both")==0) { data.webos_foreign=NULL; data.webos_foreign_table=NULL; }
    else if (strcmp(argv[1], "missing-driver")==0) dev.driverdata=NULL;
    else assert(success);
    const char *id=WaylandWebOS_CreateExportedWindow(&dev, 0);
    assert(lock_depth==0);
    if (success) {
        assert(id && strcmp(id,"test-video-window")==0);
        assert(exports==1 && allocations==1 && table.count==1);
        free(table.windows);
    } else {
        assert(!id && error_text[0]);
        assert(exports==0 && allocations==0);
    }
    return 0;
}
