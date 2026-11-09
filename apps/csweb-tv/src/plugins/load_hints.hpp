// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the app remembers about each installed plugin's last load, to start faster:
//  - the providers it registered, so the plugin behind the Home source loads first and Home can
//    request its rows as soon as that one plugin is ready (instead of after every plugin before it);
//  - the Activity type its load(Context) cast to, so the runtime loads it with a headless Activity at
//    once instead of loading it twice (CloudStream always passes its MainActivity; the first, failed
//    pass of e.g. Cricify or PlayFy fetched its configuration over the network for nothing).
// A hint belongs to one plugin build: it is ignored when the plugin's SHA-256 changes.
#pragma once

#include "plugins/plugin_store.hpp"

#include <map>
#include <string>
#include <vector>

namespace csweb::plugins {

struct LoadHint {
    std::string sha256;
    std::vector<std::string> providers;  // names, as the runtime's "provider" events give them
    std::string activity;                // e.g. androidx.appcompat.app.AppCompatActivity; empty: none
};

using LoadHints = std::map<std::string, LoadHint>;  // by plugin id

LoadHints parse_load_hints(const std::string &json_text);
std::string dump_load_hints(const LoadHints &hints);

// The hint for `p`, or null when there is none for this build of it.
const LoadHint *hint_for(const LoadHints &hints, const InstalledPlugin &p);

// The order to load `plugins` in: those whose last load registered `first_provider` first, then the
// others as installed. Indexes into `plugins`.
std::vector<size_t> load_order(const std::vector<InstalledPlugin> &plugins, const LoadHints &hints,
                               const std::string &first_provider);

// A class name the runtime may use as an Activity type: a Java binary name, nothing else.
bool valid_activity_name(const std::string &name);

}  // namespace csweb::plugins
