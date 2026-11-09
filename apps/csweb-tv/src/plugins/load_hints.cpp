// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/load_hints.hpp"

#include <nlohmann/json.hpp>

namespace csweb::plugins {

using nlohmann::json;

bool valid_activity_name(const std::string &name) {
    if (name.empty() || name.size() > 200 || name.front() == '.' || name.back() == '.') return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '$';
        if (!ok) return false;
    }
    return name.find("..") == std::string::npos;
}

LoadHints parse_load_hints(const std::string &json_text) {
    LoadHints out;
    const auto j = json::parse(json_text, nullptr, false);
    if (!j.is_object()) return out;
    for (const auto &[id, v] : j.items()) {
        if (!v.is_object() || !v.contains("sha256") || !v["sha256"].is_string()) continue;
        LoadHint h;
        h.sha256 = v["sha256"].get<std::string>();
        if (auto p = v.find("providers"); p != v.end() && p->is_array()) {
            for (const auto &name : *p) {
                if (name.is_string()) h.providers.push_back(name.get<std::string>());
            }
        }
        if (auto a = v.find("activity"); a != v.end() && a->is_string() && valid_activity_name(a->get<std::string>())) {
            h.activity = a->get<std::string>();
        }
        out[id] = std::move(h);
    }
    return out;
}

std::string dump_load_hints(const LoadHints &hints) {
    json j = json::object();
    for (const auto &[id, h] : hints) {
        json o = {{"sha256", h.sha256}, {"providers", h.providers}};
        if (!h.activity.empty()) o["activity"] = h.activity;
        j[id] = std::move(o);
    }
    return j.dump();
}

const LoadHint *hint_for(const LoadHints &hints, const InstalledPlugin &p) {
    auto it = hints.find(p.id);
    if (it == hints.end() || it->second.sha256 != p.sha256) return nullptr;
    return &it->second;
}

std::vector<size_t> load_order(const std::vector<InstalledPlugin> &plugins, const LoadHints &hints,
                               const std::string &first_provider) {
    std::vector<size_t> first, rest;
    for (size_t i = 0; i < plugins.size(); i++) {
        const LoadHint *h = first_provider.empty() ? nullptr : hint_for(hints, plugins[i]);
        bool home = false;
        if (h) {
            for (const auto &name : h->providers) home = home || name == first_provider;
        }
        (home ? first : rest).push_back(i);
    }
    first.insert(first.end(), rest.begin(), rest.end());
    return first;
}

}  // namespace csweb::plugins
