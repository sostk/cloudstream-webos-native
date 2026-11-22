// SPDX-License-Identifier: GPL-3.0-or-later
//
// A proxy for chosen providers' video (Settings → Network → Proxy): videos some services only send to
// viewers in their own country (JioTV answers HTTP 451 elsewhere). Proxies come from ProxyScrape's
// free list (https://github.com/proxyscrape/free-proxy-list, MIT; one file per country, refreshed
// every 5 minutes), are tested before use, and a proxy that fails is dropped for the next one.
// Free proxies are run by strangers: they see which hosts are reached (not HTTPS content). Only the
// player's requests for the chosen providers go through them (settings/proxy_rules). Proxy addresses are never logged with
// credentials (the list has none).
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <wnk/async/async.hpp>
#include <wnk/net/http.hpp>

namespace csweb::net {

struct ProxyCandidate {
    std::string url;  // "socks5h://1.2.3.4:1080", "http://1.2.3.4:80"
    std::string isp;
    double uptime = 0;   // percent, from the list
    int latency_ms = 0;  // from our own test (0 untested)
};

// ---- Pure helpers (tests/test_proxy_pool.cpp) ----

// ProxyScrape's per-country data.json: usable entries, best uptime first. SOCKS proxies resolve names
// on the proxy side (socks5h/socks4a) so the lookup also happens in that country.
std::vector<ProxyCandidate> parse_proxy_list(const std::string &json);
std::string proxy_list_url(const std::string &country_code);  // "in" -> the jsDelivr URL
// "socks5h://1.2.3.4:1080" -> "1.2.3.4:1080" (no scheme, no credentials) for logs and the UI.
std::string proxy_label(const std::string &proxy);

// A transport that sends every request through `proxy`.
class ProxiedTransport : public wnk::net::HttpTransport {
 public:
    ProxiedTransport(std::shared_ptr<wnk::net::HttpTransport> inner, std::string proxy)
        : inner_(std::move(inner)), proxy_(std::move(proxy)) {}
    wnk::net::HttpResponse perform(const wnk::net::HttpRequest &req, const wnk::async::CancellationToken &token) override;
    wnk::net::HttpResponse stream(const wnk::net::HttpRequest &req, const wnk::async::CancellationToken &token,
                                  const wnk::net::HttpStreamSink &sink) override;
    std::string describe() const override { return inner_->describe() + " via proxy"; }

 private:
    std::shared_ptr<wnk::net::HttpTransport> inner_;
    std::string proxy_;
};

class ProxyPool {
 public:
    ProxyPool(std::shared_ptr<wnk::net::HttpTransport> transport, wnk::async::WorkerPool &pool,
              wnk::async::MainDispatcher &dispatcher);

    // The working proxy for `country` ("in"), finding one first if needed (fetch the list, test the
    // best candidates in parallel). `on_status` reports progress; `on_done(proxy, error)` runs on the
    // main thread while `owner` is alive; proxy is empty on failure.
    void acquire(const std::string &country, wnk::async::CancellationToken token, wnk::async::Lifetime::Weak owner,
                 std::function<void(const std::string &)> on_status,
                 std::function<void(const std::string &, const std::string &)> on_done);
    // `proxy` failed (no answer, refused, still blocked): the next tested one is used from now on.
    void mark_bad(const std::string &country, const std::string &proxy);
    std::string current(const std::string &country) const;

 private:
    std::shared_ptr<wnk::net::HttpTransport> transport_;
    wnk::async::WorkerPool &pool_;
    wnk::async::MainDispatcher &dispatcher_;
    mutable std::mutex mu_;
    struct Country {
        std::vector<ProxyCandidate> working;  // tested, fastest first
        std::vector<std::string> bad;         // failed this session
    };
    std::map<std::string, Country> countries_;  // by country code
};

}  // namespace csweb::net
