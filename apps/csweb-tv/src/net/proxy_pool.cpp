// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/proxy_pool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "wnk/log.hpp"

namespace csweb::net {
namespace {

constexpr const char *kCat = "proxy";
// Reached through each candidate to test it: tiny, HTTPS (so the proxy must tunnel), always up.
constexpr const char *kProbeUrl = "https://www.gstatic.com/generate_204";
constexpr size_t kMaxProbe = 24;     // best-uptime candidates tested per round
constexpr int kProbeTimeoutMs = 8000;

}  // namespace

std::vector<ProxyCandidate> parse_proxy_list(const std::string &json) {
    std::vector<ProxyCandidate> out;
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_array()) return out;
    for (const auto &e : j) {
        if (!e.is_object()) continue;
        const std::string proto = e.value("protocol", std::string());
        const std::string ip = e.value("ip", std::string());
        int port = 0;
        if (e.contains("port") && e["port"].is_number_integer()) port = e["port"].get<int>();
        else if (e.contains("port") && e["port"].is_string()) port = std::atoi(e["port"].get<std::string>().c_str());
        if (ip.empty() || port <= 0 || port > 65535 || ip.find_first_not_of("0123456789.:abcdefABCDEF") != std::string::npos) continue;
        std::string scheme;
        if (proto == "socks5") scheme = "socks5h";
        else if (proto == "socks4") scheme = "socks4a";
        else if (proto == "http" || proto == "https") scheme = "http";
        else continue;
        ProxyCandidate c;
        c.url = scheme + "://" + ip + ":" + std::to_string(port);
        c.isp = e.value("isp", std::string());
        if (e.contains("uptime_percent") && e["uptime_percent"].is_number()) c.uptime = e["uptime_percent"].get<double>();
        out.push_back(std::move(c));
    }
    std::stable_sort(out.begin(), out.end(), [](const ProxyCandidate &a, const ProxyCandidate &b) { return a.uptime > b.uptime; });
    return out;
}

std::string proxy_list_url(const std::string &country_code) {
    return "https://cdn.jsdelivr.net/gh/proxyscrape/free-proxy-list@main/proxies/countries/" + country_code + "/data.json";
}

std::string proxy_label(const std::string &proxy) {
    std::string s = proxy;
    const size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    const size_t at = s.rfind('@');
    if (at != std::string::npos) s = s.substr(at + 1);
    return s;
}

wnk::net::HttpResponse ProxiedTransport::perform(const wnk::net::HttpRequest &req, const wnk::async::CancellationToken &token) {
    wnk::net::HttpRequest r = req;
    r.proxy = proxy_;
    return inner_->perform(r, token);
}

wnk::net::HttpResponse ProxiedTransport::stream(const wnk::net::HttpRequest &req, const wnk::async::CancellationToken &token,
                                                const wnk::net::HttpStreamSink &sink) {
    wnk::net::HttpRequest r = req;
    r.proxy = proxy_;
    return inner_->stream(r, token, sink);
}

ProxyPool::ProxyPool(std::shared_ptr<wnk::net::HttpTransport> transport, wnk::async::WorkerPool &pool,
                     wnk::async::MainDispatcher &dispatcher)
    : transport_(std::move(transport)), pool_(pool), dispatcher_(dispatcher) {}

std::string ProxyPool::current(const std::string &country) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = countries_.find(country);
    return it == countries_.end() || it->second.working.empty() ? std::string() : it->second.working.front().url;
}

void ProxyPool::mark_bad(const std::string &country, const std::string &proxy) {
    std::lock_guard<std::mutex> lock(mu_);
    Country &c = countries_[country];
    c.working.erase(std::remove_if(c.working.begin(), c.working.end(), [&](const ProxyCandidate &p) { return p.url == proxy; }),
                    c.working.end());
    if (std::find(c.bad.begin(), c.bad.end(), proxy) == c.bad.end()) c.bad.push_back(proxy);
    WNK_LOGI(kCat, "proxy %s (%s) dropped; %zu tested ones left", proxy_label(proxy).c_str(), country.c_str(), c.working.size());
}

void ProxyPool::acquire(const std::string &country, wnk::async::CancellationToken token, wnk::async::Lifetime::Weak owner,
                        std::function<void(const std::string &)> on_status,
                        std::function<void(const std::string &, const std::string &)> on_done) {
    auto status = [this, owner, on_status](std::string text) {
        dispatcher_.post([owner, on_status, text = std::move(text)]() {
            if (owner.alive() && on_status) on_status(text);
        });
    };
    auto done = [this, owner, on_done](std::string proxy, std::string error) {
        dispatcher_.post([owner, on_done, proxy = std::move(proxy), error = std::move(error)]() {
            if (owner.alive() && on_done) on_done(proxy, error);
        });
    };
    {
        std::lock_guard<std::mutex> lock(mu_);
        const Country &c = countries_[country];
        if (!c.working.empty()) {
            done(c.working.front().url, {});
            return;
        }
    }
    const bool queued = pool_.submit([this, country, status, done](const wnk::async::CancellationToken &t) {
        status("Getting the proxy list…");
        wnk::net::HttpRequest req;
        req.url = proxy_list_url(country);
        req.timeout_ms = 20000;
        const wnk::net::HttpResponse res = transport_->perform(req, t);
        if (t.cancelled()) return;
        if (res.error == wnk::net::NetError::None && res.status == 404) {  // ProxyScrape: an empty country
            done({}, "the list has no proxies for this country at the moment");
            return;
        }
        if (!res.ok()) {
            done({}, "the proxy list could not be loaded (" +
                         (res.status ? "HTTP " + std::to_string(res.status) : std::string(wnk::net::error_name(res.error))) + ")");
            return;
        }
        std::vector<ProxyCandidate> all = parse_proxy_list(res.body);
        {
            std::lock_guard<std::mutex> lock(mu_);
            const std::vector<std::string> &bad = countries_[country].bad;
            all.erase(std::remove_if(all.begin(), all.end(),
                                     [&](const ProxyCandidate &c) {
                                         return std::find(bad.begin(), bad.end(), c.url) != bad.end();
                                     }),
                      all.end());
        }
        if (all.size() > kMaxProbe) all.resize(kMaxProbe);
        if (all.empty()) {
            done({}, "the list has no proxies for this country");
            return;
        }
        WNK_LOGI(kCat, "testing %zu proxies for %s", all.size(), country.c_str());
        status("Testing " + std::to_string(all.size()) + " proxies…");
        // All at once, each with a short limit: free proxies are mostly dead, and a round should take
        // seconds, not minutes.
        std::vector<std::thread> threads;
        std::atomic<int> finished{0};
        for (ProxyCandidate &c : all) {
            threads.emplace_back([this, &c, &t, &finished]() {
                wnk::net::HttpRequest probe;
                probe.url = kProbeUrl;
                probe.proxy = c.url;
                probe.timeout_ms = kProbeTimeoutMs;
                probe.connect_timeout_ms = kProbeTimeoutMs;
                probe.max_redirects = 0;
                const auto start = std::chrono::steady_clock::now();
                const wnk::net::HttpResponse r = transport_->perform(probe, t);
                if (r.ok()) {
                    c.latency_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::steady_clock::now() - start)
                                                        .count());
                    if (c.latency_ms == 0) c.latency_ms = 1;
                }
                finished++;
            });
        }
        for (auto &th : threads) th.join();
        if (t.cancelled()) return;
        std::vector<ProxyCandidate> good;
        for (const auto &c : all) {
            if (c.latency_ms > 0) good.push_back(c);
        }
        std::sort(good.begin(), good.end(), [](const ProxyCandidate &a, const ProxyCandidate &b) { return a.latency_ms < b.latency_ms; });
        WNK_LOGI(kCat, "%zu of %zu proxies answered", good.size(), all.size());
        if (good.empty()) {
            done({}, "none of the " + std::to_string(all.size()) + " proxies answered");
            return;
        }
        std::string first;
        {
            std::lock_guard<std::mutex> lock(mu_);
            countries_[country].working = good;
            first = good.front().url;
        }
        WNK_LOGI(kCat, "using %s (%s, %d ms)", proxy_label(first).c_str(), good.front().isp.c_str(), good.front().latency_ms);
        done(first, {});
    }, token);
    if (!queued) done({}, "busy");
}

}  // namespace csweb::net
