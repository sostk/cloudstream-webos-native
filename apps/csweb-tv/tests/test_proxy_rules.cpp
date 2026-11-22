// SPDX-License-Identifier: GPL-3.0-or-later
// settings/proxy_rules: typed proxy addresses and the per-provider rules' JSON.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "settings/proxy_rules.hpp"

using namespace csweb::settings;

namespace {
std::string norm(const std::string &typed) {
    std::string out, error;
    return normalize_proxy_url(typed, &out, &error) ? out : "ERR: " + error;
}
}  // namespace

TEST("typed proxy addresses") {
    CHECK_EQ(norm("1.2.3.4:8080"), std::string("http://1.2.3.4:8080"));
    CHECK_EQ(norm("  socks5://1.2.3.4:1080/ "), std::string("socks5h://1.2.3.4:1080"));
    CHECK_EQ(norm("SOCKS4://proxy.example.org:1080"), std::string("socks4a://proxy.example.org:1080"));
    CHECK_EQ(norm("https://me:p%40ss@vpn.example:443"), std::string("https://me:p%40ss@vpn.example:443"));
    CHECK_EQ(norm("socks5h://u:p@[2001:db8::1]:1080"), std::string("socks5h://u:p@[2001:db8::1]:1080"));
    CHECK(norm("").rfind("ERR", 0) == 0);
    CHECK(norm("1.2.3.4").rfind("ERR", 0) == 0);            // no port
    CHECK(norm("1.2.3.4:99999").rfind("ERR", 0) == 0);
    CHECK(norm("ftp://1.2.3.4:21").rfind("ERR", 0) == 0);
    CHECK(norm("1.2.3.4:80;rm -rf").rfind("ERR", 0) == 0);
    CHECK(norm("bad host:80").rfind("ERR", 0) == 0);
}

TEST("rules: free per country, personal, off not kept, bad entries dropped") {
    ProxyRules rules;
    rules["Jio Tv +"] = {ProxyRule::Type::Free, "in", ""};
    rules["Other"] = {ProxyRule::Type::Personal, "in", "socks5h://u:p@1.2.3.4:1080"};
    rules["Direct"] = {ProxyRule::Type::Off, "in", ""};
    const ProxyRules back = proxy_rules_from_json(proxy_rules_to_json(rules));
    REQUIRE(back.size() == 2);
    CHECK(back.at("Jio Tv +").type == ProxyRule::Type::Free);
    CHECK_EQ(back.at("Jio Tv +").country, std::string("in"));
    CHECK_EQ(back.at("Other").url, std::string("socks5h://u:p@1.2.3.4:1080"));
    const ProxyRules bad = proxy_rules_from_json(
        R"({"a":{"type":"free","country":"INDIA"},"b":{"type":"personal","url":"nope"},"c":7,"d":{"type":"x"}})");
    CHECK(bad.empty());
    CHECK(proxy_rules_from_json("not json").empty());
}
