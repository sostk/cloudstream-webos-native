// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/plugin_store.hpp"

#include "core/sha256.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace csweb::plugins {

namespace {

using nlohmann::json;
using wnk::storage::Status;

constexpr const char *kReposKey = "csweb.repos.v1";
constexpr const char *kIndexKey = "csweb.plugins.v1";
constexpr const char *kLoadHintsKey = "csweb.loadhints.v1";

std::string blob_name(const std::string &id) { return "plugin-" + id; }

json to_json(const InstalledPlugin &p) {
    return {{"id", p.id}, {"internalName", p.internal_name}, {"name", p.name}, {"repositoryUrl", p.repository_url},
            {"url", p.url}, {"version", p.version}, {"sha256", p.sha256}, {"size", p.size}, {"tvTypes", p.tv_types},
            {"language", p.language}, {"iconUrl", p.icon_url}, {"enabled", p.enabled}};
}

InstalledPlugin from_json(const json &j) {
    InstalledPlugin p;
    p.id = j.value("id", "");
    p.internal_name = j.value("internalName", "");
    p.name = j.value("name", "");
    p.repository_url = j.value("repositoryUrl", "");
    p.url = j.value("url", "");
    p.version = j.value("version", 0);
    p.sha256 = j.value("sha256", "");
    p.size = j.value("size", 0LL);
    if (j.contains("tvTypes") && j["tvTypes"].is_array()) {
        for (const auto &t : j["tvTypes"]) if (t.is_string()) p.tv_types.push_back(t.get<std::string>());
    }
    p.language = j.value("language", "");
    p.icon_url = j.value("iconUrl", "");
    p.enabled = j.value("enabled", true);
    return p;
}

bool valid_id(const std::string &id) {
    if (id.size() != 16) return false;
    for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

}  // namespace

PluginStore::PluginStore(wnk::storage::KeyValueStore &kv) : kv_(kv), blobs_(kv, 32u << 20) {}

std::vector<RepositoryData> PluginStore::repositories() {
    std::vector<RepositoryData> out;
    wnk::storage::Entry e;
    if (kv_.get(kReposKey, &e) != Status::Ok) return out;
    const auto j = json::parse(e.value, nullptr, false);
    if (!j.is_array()) return out;
    for (const auto &r : j) {
        if (r.is_object() && r.contains("url")) out.push_back({r.value("name", ""), r.value("url", ""), r.value("iconUrl", "")});
    }
    return out;
}

bool PluginStore::save_repositories(const std::vector<RepositoryData> &repos, std::string *error) {
    json j = json::array();
    for (const auto &r : repos) j.push_back({{"name", r.name}, {"url", r.url}, {"iconUrl", r.icon_url}});
    const auto res = kv_.put(kReposKey, j.dump());
    if (res.status != Status::Ok && error) *error = wnk::storage::status_name(res.status);
    return res.status == Status::Ok;
}

std::vector<InstalledPlugin> PluginStore::installed() {
    std::vector<InstalledPlugin> out;
    wnk::storage::Entry e;
    if (kv_.get(kIndexKey, &e) != Status::Ok) return out;
    const auto j = json::parse(e.value, nullptr, false);
    if (!j.is_array()) return out;
    for (const auto &o : j) {
        if (!o.is_object()) continue;
        InstalledPlugin p = from_json(o);
        if (valid_id(p.id) && core::is_sha256_hex(p.sha256)) out.push_back(std::move(p));
    }
    return out;
}

std::string PluginStore::load_hints() {
    wnk::storage::Entry e;
    if (kv_.get(kLoadHintsKey, &e) != Status::Ok) return "{}";
    return e.value;
}

bool PluginStore::save_load_hints(const std::string &json_text, std::string *error) {
    const auto res = kv_.put(kLoadHintsKey, json_text);
    if (res.status != Status::Ok && error) *error = wnk::storage::status_name(res.status);
    return res.status == Status::Ok;
}

bool PluginStore::save_index(const std::vector<InstalledPlugin> &list, std::string *error) {
    json j = json::array();
    for (const auto &p : list) j.push_back(to_json(p));
    const auto res = kv_.put(kIndexKey, j.dump());
    if (res.status != Status::Ok && error) *error = wnk::storage::status_name(res.status);
    return res.status == Status::Ok;
}

bool PluginStore::install(const InstalledPlugin &record, const std::string &bytes, std::string *error) {
    if (!valid_id(record.id) || !core::is_sha256_hex(record.sha256)) {
        if (error) *error = "invalid plugin record";
        return false;
    }
    if (core::Sha256::hex(bytes) != record.sha256) {
        if (error) *error = "hash does not match the bytes";
        return false;
    }
    const Status st = blobs_.put(blob_name(record.id), bytes);
    if (st != Status::Ok) {
        if (error) *error = std::string("store plugin: ") + wnk::storage::status_name(st);
        return false;
    }
    auto list = installed();
    bool replaced = false;
    for (auto &p : list) {
        if (p.id == record.id) {
            p = record;
            replaced = true;
        }
    }
    if (!replaced) list.push_back(record);
    return save_index(list, error);
}

bool PluginStore::remove(const std::string &id, std::string *error) {
    auto list = installed();
    std::vector<InstalledPlugin> keep;
    for (auto &p : list) if (p.id != id) keep.push_back(std::move(p));
    if (!save_index(keep, error)) return false;
    blobs_.remove(blob_name(id));  // a leftover blob is harmless and replaced on reinstall
    return true;
}

bool PluginStore::set_enabled(const std::string &id, bool enabled, std::string *error) {
    auto list = installed();
    for (auto &p : list) if (p.id == id) p.enabled = enabled;
    return save_index(list, error);
}

std::string PluginStore::materialize(const InstalledPlugin &p, const std::string &dir, std::string *error) {
    if (!valid_id(p.id)) {
        if (error) *error = "invalid plugin id";
        return {};
    }
    std::string bytes;
    const Status st = blobs_.get(blob_name(p.id), &bytes);
    if (st != Status::Ok) {
        if (error) *error = std::string("read plugin: ") + wnk::storage::status_name(st);
        return {};
    }
    if (core::Sha256::hex(bytes) != p.sha256) {
        if (error) *error = "stored plugin does not match its recorded hash";
        return {};
    }
    mkdir(dir.c_str(), 0700);
    const std::string path = dir + "/" + p.id + ".cs3";
    const std::string tmp = path + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        if (error) *error = std::string("write plugin: ") + std::strerror(errno);
        return {};
    }
    size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + off, bytes.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (error) *error = std::string("write plugin: ") + std::strerror(errno);
            ::close(fd);
            ::unlink(tmp.c_str());
            return {};
        }
        off += size_t(n);
    }
    ::close(fd);
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (error) *error = std::string("write plugin: ") + std::strerror(errno);
        ::unlink(tmp.c_str());
        return {};
    }
    return path;
}

}  // namespace csweb::plugins
