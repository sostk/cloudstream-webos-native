// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which providers' videos go through a proxy, and which (Settings → Network → Proxies): per provider,
// off, a free public proxy in a chosen country (net/proxy_pool), or the user's own proxy server.
// Stored as JSON in one setting. A personal proxy URL may carry a login: it is kept in the app's
// settings on the TV and never logged (net::proxy_label strips it for the UI and logs).
#pragma once

#include <map>
#include <string>

#include "settings/settings_store.hpp"

namespace csweb::settings {

inline constexpr const char *kProxyRules = "csweb_proxy_rules";

struct ProxyRule {
    enum class Type { Off, Free, Personal };
    Type type = Type::Off;
    std::string country = "in";  // Free: the proxies' country code
    std::string url;             // Personal: normalized ("socks5h://user:pass@host:port")
};

using ProxyRules = std::map<std::string, ProxyRule>;  // provider name → rule (Off rules are not kept)

ProxyRules read_proxy_rules(const SettingsStore &s);
void write_proxy_rules(SettingsStore &s, const ProxyRules &rules);
ProxyRule proxy_rule_for(const SettingsStore &s, const std::string &provider);
void set_proxy_rule(SettingsStore &s, const std::string &provider, const ProxyRule &rule);

// A proxy as typed: "host:port" (HTTP), or with http://, https://, socks4(a)://, socks5(h)://, and an
// optional user:password@. SOCKS proxies resolve names themselves (socks4a/socks5h), so lookups
// also happen in the proxy's country. False with a reason when it is not a usable proxy address.
bool normalize_proxy_url(const std::string &typed, std::string *out, std::string *error);

// The JSON form (tests).
std::string proxy_rules_to_json(const ProxyRules &rules);
ProxyRules proxy_rules_from_json(const std::string &json);

}  // namespace csweb::settings
