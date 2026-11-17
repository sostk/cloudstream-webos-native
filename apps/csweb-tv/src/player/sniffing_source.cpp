// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/sniffing_source.hpp"

#include "wnk/log.hpp"

#include <algorithm>
#include <cctype>

namespace csweb::player {

using namespace wnk::media;

namespace {
constexpr size_t kSniffBytes = 2048;

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string_view trim_start(std::string_view s) {
    if (s.substr(0, 3) == "\xEF\xBB\xBF") s.remove_prefix(3);  // UTF-8 BOM
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    return s;
}
}  // namespace

bool looks_like_manifest(const std::string &content_type, std::string_view head) {
    std::string type = lower(content_type.substr(0, content_type.find(';')));
    type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char c) { return std::isspace(c); }), type.end());
    if (type == "application/vnd.apple.mpegurl" || type == "application/x-mpegurl" || type == "audio/mpegurl" ||
        type == "audio/x-mpegurl" || type == "application/dash+xml") {
        return true;
    }
    head = trim_start(head);
    if (head.substr(0, 7) == "#EXTM3U") return true;
    // XML: skip the declaration, comments and processing instructions to the root element.
    while (head.substr(0, 2) == "<?" || head.substr(0, 4) == "<!--") {
        const size_t end = head.find(head[1] == '?' ? "?>" : "-->");
        if (end == std::string_view::npos) return false;
        head = trim_start(head.substr(end + (head[1] == '?' ? 2 : 3)));
    }
    if (head.substr(0, 4) != "<MPD") return false;
    const char next = head.size() > 4 ? head[4] : ' ';
    return std::isspace(static_cast<unsigned char>(next)) || next == '>';
}

SniffingSource::SniffingSource(std::unique_ptr<MediaSource> file, std::shared_ptr<wnk::net::HttpTransport> http,
                               wnk::net::Headers headers, Factory make_adaptive)
    : file_(std::move(file)), http_(std::move(http)), headers_(std::move(headers)), make_adaptive_(std::move(make_adaptive)) {}

bool SniffingSource::open(const std::string &uri, std::string *error) {
    if (file_->open(uri, error)) return true;
    const bool web = uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0;
    if (interrupted_ || !web || !http_ || !make_adaptive_ || !sniff(uri)) return false;
    WNK_LOGI("csweb", "a VIDEO link answered with a playlist: playing it as HLS/DASH");
    std::unique_ptr<MediaSource> a = make_adaptive_();
    MediaSource *raw = a.get();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        adaptive_ = std::move(a);
    }
    if (interrupted_) raw->interrupt();  // an interrupt that came while switching
    return raw->open(uri, error);
}

bool SniffingSource::sniff(const std::string &uri) {
    wnk::net::HttpRequest req;
    req.url = uri;
    req.headers = headers_;
    req.timeout_ms = 15000;
    std::string content_type, head;
    wnk::async::CancellationToken token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token = cancel_.token();
    }
    wnk::net::HttpStreamSink sink;
    sink.on_response = [&](long status, const wnk::net::Headers &headers) {
        if (status < 200 || status >= 300) return false;
        for (const auto &[name, value] : headers) {
            if (lower(name) == "content-type") content_type = value;
        }
        return true;
    };
    sink.on_data = [&](const uint8_t *data, size_t size) {
        head.append(reinterpret_cast<const char *>(data), std::min(size, kSniffBytes - head.size()));
        return head.size() < kSniffBytes && !looks_like_manifest(content_type, head);  // stop once decided
    };
    http_->stream(req, token, sink);  // stopped early on purpose: judged on what arrived
    return !token.cancelled() && looks_like_manifest(content_type, head);
}

void SniffingSource::interrupt() {
    interrupted_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_.cancel();
    file_->interrupt();
    if (adaptive_) adaptive_->interrupt();
}

void SniffingSource::reset_interrupt() {
    interrupted_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_ = wnk::async::CancellationSource();
    file_->reset_interrupt();
    if (adaptive_) adaptive_->reset_interrupt();
}

}  // namespace csweb::player
