// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings/proxy_rules.hpp"

#include <cctype>

#include <nlohmann/json.hpp>

namespace csweb::settings {
namespace {

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool valid_country(const std::string &c) {
    return c.size() == 2 && std::islower(static_cast<unsigned char>(c[0])) && std::islower(static_cast<unsigned char>(c[1]));
}

}  // namespace

bool normalize_proxy_url(const std::string &typed, std::string *out, std::string *error) {
    std::string s = typed;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) {
        *error = "Type the proxy's address";
        return false;
    }
    std::string scheme = "http";
    const size_t sep = s.find("://");
    if (sep != std::string::npos) {
        scheme = lower(s.substr(0, sep));
        s = s.substr(sep + 3);
    }
    if (scheme == "socks5") scheme = "socks5h";
    else if (scheme == "socks4") scheme = "socks4a";
    else if (scheme != "http" && scheme != "https" && scheme != "socks5h" && scheme != "socks4a") {
        *error = "Unknown proxy type \"" + scheme + "\" (use http, https, socks4 or socks5)";
        return false;
    }
    while (!s.empty() && s.back() == '/') s.pop_back();
    std::string login;
    const size_t at = s.rfind('@');
    if (at != std::string::npos) {
        login = s.substr(0, at);
        s = s.substr(at + 1);
        if (login.empty() || login.find_first_of(" /\t") != std::string::npos) {
            *error = "The login before @ is not valid";
            return false;
        }
    }
    const size_t colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size()) {
        *error = "Add the port, like 1.2.3.4:8080";
        return false;
    }
    const std::string host = s.substr(0, colon), port = s.substr(colon + 1);
    for (char c : host) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '[' || c == ']' || c == ':')) {
            *error = "The host is not valid";
            return false;
        }
    }
    if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos || std::stoi(port) < 1 ||
        std::stoi(port) > 65535) {
        *error = "The port must be a number from 1 to 65535";
        return false;
    }
    *out = scheme + "://" + (login.empty() ? "" : login + "@") + host + ":" + port;
    return true;
}

std::string proxy_rules_to_json(const ProxyRules &rules) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto &[provider, r] : rules) {
        if (r.type == ProxyRule::Type::Free) j[provider] = {{"type", "free"}, {"country", r.country}};
        else if (r.type == ProxyRule::Type::Personal) j[provider] = {{"type", "personal"}, {"url", r.url}};
    }
    return j.dump();
}

ProxyRules proxy_rules_from_json(const std::string &json) {
    ProxyRules out;
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object()) return out;
    for (const auto &[provider, e] : j.items()) {
        if (!e.is_object()) continue;
        ProxyRule r;
        const std::string type = e.value("type", std::string());
        if (type == "free") {
            r.type = ProxyRule::Type::Free;
            r.country = e.value("country", std::string("in"));
            if (!valid_country(r.country)) continue;
        } else if (type == "personal") {
            r.type = ProxyRule::Type::Personal;
            std::string error;
            if (!normalize_proxy_url(e.value("url", std::string()), &r.url, &error)) continue;
        } else {
            continue;
        }
        out[provider] = r;
    }
    return out;
}

ProxyRules read_proxy_rules(const SettingsStore &s) { return proxy_rules_from_json(s.get_string(kProxyRules, "{}")); }

void write_proxy_rules(SettingsStore &s, const ProxyRules &rules) { s.set(kProxyRules, proxy_rules_to_json(rules)); }

ProxyRule proxy_rule_for(const SettingsStore &s, const std::string &provider) {
    const ProxyRules rules = read_proxy_rules(s);
    const auto it = rules.find(provider);
    return it == rules.end() ? ProxyRule{} : it->second;
}

void set_proxy_rule(SettingsStore &s, const std::string &provider, const ProxyRule &rule) {
    ProxyRules rules = read_proxy_rules(s);
    if (rule.type == ProxyRule::Type::Off) rules.erase(provider);
    else rules[provider] = rule;
    write_proxy_rules(s, rules);
}

}  // namespace csweb::settings
