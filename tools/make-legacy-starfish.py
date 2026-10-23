#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate an app-owned legacy backend from the unmodified, pinned kit.

Only CSWEB_LEGACY + WNK_WEBOS invokes this. Fail closed on kit changes; review the
integration before moving the pin. No kit or main-build source is rewritten.
"""
import hashlib
import pathlib
import sys

PINS = {
    "media/src/starfish_backend.cpp": "a01f8d2d088c953fd36dd434142f85ca5a87552e95bbf6f2f198a31c1e878947",
    "media/include/wnk/media/starfish_backend.hpp": "e5ba9ce48debc32f17975b5e3a9df608cba84ff08db1ecbfbaed82fb36af4650",
}


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("legacy Starfish: source anchor changed: " + before[:80])
    return text.replace(before, after, 1)


def generate(kit, output):
    texts = {}
    for path, digest in PINS.items():
        data = (kit / path).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("legacy Starfish: pinned kit source changed: " + path)
        texts[path] = data.decode()
    source = texts["media/src/starfish_backend.cpp"]
    header = texts["media/include/wnk/media/starfish_backend.hpp"]
    for old, new in (("StarfishBackend", "LegacyStarfishBackend"),
                     ("StarfishOptions", "LegacyStarfishOptions"), ("StarfishLoad", "LegacyStarfishLoad")):
        source = source.replace(old, new)
        header = header.replace(old, new)
    source = replace_once(source, '#include "wnk/media/starfish_backend.hpp"',
                          '#include "player/legacy_starfish_backend.hpp"')
    header = replace_once(header, '#include "wnk/media/pts_splice.hpp"',
                          '#include "wnk/media/pts_splice.hpp"\n#include "player/legacy_acb.hpp"')
    header = replace_once(header, '    std::string window_id_;',
                          '    csweb::player::LegacyAcb acb_;\n    std::string window_id_;')
    source = replace_once(source, '    std::string why;',
                          '    const char *(*get_media_id)(void *self) = nullptr;  // required only for ACB\n    std::string why;')
    before = '''    ok = sym(RTLD_DEFAULT, g_api.create_window, "SDL_webOSCreateExportedWindow") && ok;
    ok = sym(RTLD_DEFAULT, g_api.set_window, "SDL_webOSSetExportedWindow") && ok;
    ok = sym(RTLD_DEFAULT, g_api.destroy_window, "SDL_webOSDestroyExportedWindow") && ok;'''
    after = '''    g_api.get_media_id = reinterpret_cast<decltype(g_api.get_media_id)>(dlsym(h, "_ZN17StarfishMediaAPIs10getMediaIDEv"));
    // SDL entry points are optional on old firmware. A successful create, rather
    // than the presence of a symbol, proves the compositor supports export.
    g_api.create_window = reinterpret_cast<decltype(g_api.create_window)>(dlsym(RTLD_DEFAULT, "SDL_webOSCreateExportedWindow"));
    g_api.set_window = reinterpret_cast<decltype(g_api.set_window)>(dlsym(RTLD_DEFAULT, "SDL_webOSSetExportedWindow"));
    g_api.destroy_window = reinterpret_cast<decltype(g_api.destroy_window)>(dlsym(RTLD_DEFAULT, "SDL_webOSDestroyExportedWindow"));'''
    source = replace_once(source, before, after)
    source = replace_once(source, '    if (const char *id = api().create_window(0)) {',
                          '    const bool can_export = api().create_window && api().set_window && api().destroy_window;\n'
                          '    if (const char *id = can_export ? api().create_window(0) : nullptr) {')
    source = replace_once(source, '        WNK_LOGW(kCat, "SDL_webOSCreateExportedWindow failed: the video will not be visible");',
                          '        WNK_LOGI(kCat, "exported window unavailable; will try legacy ACB display control");')
    source = replace_once(source, '    if (type == 23) {  // unload completed: the destructor may run',
                          '    acb_.event(type, str);  // mailbox only; never call LG back from its callback\n'
                          '    if (type == 23) {  // unload completed: the destructor may run')
    source = replace_once(source, '    unload();\n    void *object = nullptr;',
                          '''    unload();
    if (window_id_.empty()) {
        if (!api().get_media_id) {
            *error = "legacy ACB requires StarfishMediaAPIs::getMediaID";
            return false;
        }
        if (!acb_.open(options_.app_id, error)) return false;
    }
    void *object = nullptr;''')
    source = replace_once(source, '        *error = "out of memory";',
                          '        acb_.close();\n        *error = "out of memory";')
    source = replace_once(source, '        *error = "StarfishMediaAPIs::Load refused the stream";',
                          '        acb_.close();\n        *error = "StarfishMediaAPIs::Load refused the stream";')
    source = replace_once(source, '    // Placing the window is an SDL call: the main thread does it through set_area().',
                          '''    // Load may callback synchronously: ACB work starts only after it returns.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (window_id_.empty()) acb_.set_area(area_, format.video->width, format.video->height);
    }
    if (window_id_.empty() && !acb_.pump(api().get_media_id(object), error)) {
        unload();
        return false;
    }
    // Placing an exported window is an SDL call: the main thread uses set_area().''')
    source = replace_once(source, '    if (play) WNK_LOGI(kCat, "Play -> %d", api().play(object));',
                          '''    std::string acb_error;
    if (window_id_.empty() && !acb_.pump(api().get_media_id(object), &acb_error)) return Feed::Error;
    if (play) WNK_LOGI(kCat, "Play -> %d", api().play(object));''')
    source = replace_once(source, '    if (completed) api().dtor(object);',
                          '    acb_.close();\n    if (completed) api().dtor(object);')
    source = replace_once(source, '    const int rc = paused ? api().pause(object) : api().play(object);',
                          '''    const int rc = paused ? api().pause(object) : api().play(object);
    if (window_id_.empty()) {
        acb_.transport(paused);
        std::string error;
        if (!acb_.pump(api().get_media_id(object), &error)) return false;
    }''')
    source = replace_once(source, '    if (window_id_.empty() || !usable()) return;',
                          '''    if (window_id_.empty()) {
        acb_.set_area(area_, format_.video ? format_.video->width : 0,
                      format_.video ? format_.video->height : 0);  // mailbox only
        return;
    }
    if (!usable()) return;''')
    directory = output / "player"
    directory.mkdir(parents=True, exist_ok=True)
    for name, content in (("legacy_starfish_backend.hpp", header), ("legacy_starfish_backend.cpp", source)):
        path = directory / name
        # Preserve timestamps across configure when no input changed.
        if not path.exists() or path.read_text() != content:
            path.write_text(content)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: make-legacy-starfish.py KIT_SOURCE OUTPUT_DIR")
    try:
        generate(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
