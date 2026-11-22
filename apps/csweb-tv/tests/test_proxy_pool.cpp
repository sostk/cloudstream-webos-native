// SPDX-License-Identifier: GPL-3.0-or-later
// net/proxy_pool's pure parts: ProxyScrape's list, its URL, labels without credentials.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "net/proxy_pool.hpp"

using namespace csweb::net;

TEST("ProxyScrape's list: usable entries, best uptime first, names resolved on the proxy") {
    const std::string json = R"([
        {"protocol":"http","ip":"122.166.79.199","port":80,"isp":"Bharti Airtel","uptime_percent":69.7},
        {"protocol":"socks5","ip":"103.124.172.134","port":1080,"isp":"Shirsty","uptime_percent":94.5},
        {"protocol":"socks4","ip":"103.137.148.253","port":"1080","uptime_percent":55.1},
        {"protocol":"ftp","ip":"1.2.3.4","port":21},
        {"protocol":"http","ip":"evil.example;rm","port":80},
        {"protocol":"http","ip":"1.2.3.4","port":70000}
    ])";
    const auto l = parse_proxy_list(json);
    REQUIRE(l.size() == 3);
    CHECK_EQ(l[0].url, std::string("socks5h://103.124.172.134:1080"));
    CHECK_EQ(l[1].url, std::string("http://122.166.79.199:80"));
    CHECK_EQ(l[2].url, std::string("socks4a://103.137.148.253:1080"));
    CHECK_EQ(l[0].isp, std::string("Shirsty"));
    CHECK(parse_proxy_list("not json").empty());
    CHECK(parse_proxy_list("{}").empty());
}

TEST("list URL and labels") {
    CHECK_EQ(proxy_list_url("in"),
             std::string("https://cdn.jsdelivr.net/gh/proxyscrape/free-proxy-list@main/proxies/countries/in/data.json"));
    CHECK_EQ(proxy_label("socks5h://user:secret@1.2.3.4:1080"), std::string("1.2.3.4:1080"));
    CHECK_EQ(proxy_label("http://1.2.3.4:80"), std::string("1.2.3.4:80"));
}
