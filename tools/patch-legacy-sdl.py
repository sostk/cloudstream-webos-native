#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard the legacy SDL exported-window path when the compositor omits its protocol."""
import pathlib
import sys

ANCHOR = """    if ((window = SDL_GL_GetCurrentWindow()) == NULL) {"""
GUARD = """    /* CSWeb legacy addition: older compositors may not advertise wl_webos_foreign.
     * SDL only allocates the table when that registry global is announced. */
    if (data->webos_foreign == NULL || data->webos_foreign_table == NULL) {
        SDL_SetError("Failed creating exported window: wl_webos_foreign unavailable");
        return NULL;
    }
"""


def patch_text(source):
    if source.count(GUARD) == 1:
        if source.count(ANCHOR) != 1 or source.index(GUARD) > source.index(ANCHOR):
            raise ValueError("legacy SDL guard: unexpected existing patch")
        return source
    if "CSWeb legacy addition" in source or source.count(ANCHOR) != 1:
        raise ValueError("legacy SDL guard: pinned source anchor changed")
    return source.replace(ANCHOR, GUARD + ANCHOR, 1)


def main(directory):
    source = pathlib.Path(directory) / "src/video/wayland/SDL_waylandwebos_foreign.c"
    before = source.read_text()
    after = patch_text(before)
    if after != before:
        source.write_text(after)
    print("legacy SDL exported-window guard verified")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch-legacy-sdl.py SDL_SOURCE_DIR")
    main(sys.argv[1])
