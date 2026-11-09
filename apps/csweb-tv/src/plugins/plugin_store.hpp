// SPDX-License-Identifier: GPL-3.0-or-later
//
// Durable record of repositories and installed plugins. The Developer Mode TV has no durable writable
// filesystem, so a plugin's bytes live in the key/value store (DB8, as chunked blobs) and are written
// out to tmpfs for the runtime at each start, after their SHA-256 is checked against the hash recorded
// at install. Mirrors CloudStream's REPOSITORIES_KEY and PLUGINS_KEY (PluginData: internalName, url,
// version, file) plus the hash and size this app keeps.
//
// Every method blocks (DB8 round trips on the TV): call from a worker, never from draw or a handler.
#pragma once

#include "plugins/repository.hpp"

#include "wnk/storage/blob.hpp"
#include "wnk/storage/kv.hpp"

#include <string>
#include <vector>

namespace csweb::plugins {

struct InstalledPlugin {
    std::string id;              // plugin_id(repository_url, internal_name)
    std::string internal_name, name, repository_url, url;
    int version = 0;             // from the plugin list at install (CloudStream reads the manifest later)
    std::string sha256;          // of the stored bytes, recorded at install
    long long size = 0;
    std::vector<std::string> tv_types;
    std::string language, icon_url;
    bool enabled = true;
};

class PluginStore {
 public:
    explicit PluginStore(wnk::storage::KeyValueStore &kv);

    std::vector<RepositoryData> repositories();
    bool save_repositories(const std::vector<RepositoryData> &repos, std::string *error);

    std::vector<InstalledPlugin> installed();
    // Stores the bytes then the record (a crash in between leaves an unreferenced blob, removed by the
    // next install of that id). `bytes` must already have passed the archive check.
    bool install(const InstalledPlugin &record, const std::string &bytes, std::string *error);
    bool remove(const std::string &id, std::string *error);
    bool set_enabled(const std::string &id, bool enabled, std::string *error);

    // Writes the plugin to `<dir>/<id>.cs3` (0600) if its stored bytes still hash to the recorded
    // value; returns the path, or empty with `error`.
    std::string materialize(const InstalledPlugin &p, const std::string &dir, std::string *error);

    // plugins/load_hints.hpp as JSON text ("{}" when none or unreadable).
    std::string load_hints();
    bool save_load_hints(const std::string &json_text, std::string *error);

 private:
    bool save_index(const std::vector<InstalledPlugin> &list, std::string *error);
    wnk::storage::KeyValueStore &kv_;
    wnk::storage::BlobStore blobs_;
};

}  // namespace csweb::plugins
