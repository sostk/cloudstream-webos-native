/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* Contributed by Mariotaku <mariotaku.lee@gmail.com> */

/* Regression fixture: original create function from webosbrew/SDL-webOS,
 * release-2.30.12-webos.6, src/video/wayland/SDL_waylandwebos_foreign.c.
 * The test supplies a minimal video/compositor environment around this function. */

const char *WaylandWebOS_CreateExportedWindow(_THIS, SDL_webOSExportedWindowType type)
{
    SDL_VideoData *data = _this->driverdata;
    SDL_Window *window;
    SDL_WindowData *window_data;
    webos_foreign_window *foreign_window;

    if (_this->driverdata == NULL) {
        SDL_SetError("Failed creating exported window: No video driver data for video device");
        return NULL;
    }
    if ((window = SDL_GL_GetCurrentWindow()) == NULL) {
        window = _this->windows;
        while (window != NULL) {
            if (window->driverdata != NULL) {
                break;
            }
            window = window->next;
        }
    }

    if (window == NULL) {
        SDL_SetError("Failed creating exported window: No current window");
        return NULL;
    }

    if (!(window->flags & SDL_WINDOW_OPENGL)) {
        if (SDL_RecreateWindow(window, window->flags | SDL_WINDOW_OPENGL) != 0) {
            SDL_SetError("Failed creating exported window: Failed to recreate window with OpenGL");
            return NULL;
        }
    }

    window_data = window->driverdata;
    if (window_data == NULL || window_data->surface == NULL) {
        SDL_SetError("Failed creating exported window: No surface for window");
        return NULL;
    }
    if ((uint32_t)type > SDL_WEBOS_EXPORTED_WINDOW_TYPE_OPAQUE) {
        SDL_SetError("Failed creating exported window: Invalid type");
        return NULL;
    }

    SDL_LockMutex(_this->webos_foreign_lock);
    foreign_window = SDL_calloc(1, sizeof(webos_foreign_window));
    if (foreign_window == NULL) {
        SDL_SetError("Failed creating exported window: Failed allocating memory");
        SDL_UnlockMutex(_this->webos_foreign_lock);
        return NULL;
    }

    if (data->webos_foreign_table->windows != NULL) {
        webos_foreign_window *cur = data->webos_foreign_table->windows;
        while (cur->next != NULL) {
            cur = cur->next;
        }
        cur->next = foreign_window;
    } else {
        data->webos_foreign_table->windows = foreign_window;
    }
    data->webos_foreign_table->count += 1;
    foreign_window->exported = wl_webos_foreign_export_element(data->webos_foreign, window_data->surface, type);
    wl_webos_exported_add_listener(foreign_window->exported, &exported_listener, foreign_window);
    // This is a pretty bad idea, but LG did it even worse - they create a detached thread per 10 ms
    while (foreign_window->window_id[0] == '\0') {
        SDL_Delay(10);
        WAYLAND_wl_display_dispatch(data->display);
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Created exported window %s", foreign_window->window_id);
    SDL_UnlockMutex(_this->webos_foreign_lock);
    return foreign_window->window_id;
}
