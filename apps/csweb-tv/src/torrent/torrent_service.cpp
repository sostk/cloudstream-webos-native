// SPDX-License-Identifier: GPL-3.0-or-later
#include "torrent/torrent_service.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include <thread>

#include <nlohmann/json.hpp>
#include "wnk/log.hpp"

namespace csweb::torrent {
namespace {

constexpr const char *kCat = "torrent";

// CloudStream's list (Torrent.kt `trackers`).
const char *const kTrackers[] = {
    "udp://tracker.opentrackr.org:1337/announce",
    "https://tracker2.ctix.cn/announce",
    "https://tracker1.520.jp:443/announce",
    "udp://opentracker.i2p.rocks:6969/announce",
    "udp://open.tracker.cl:1337/announce",
    "udp://open.demonii.com:1337/announce",
    "http://tracker.openbittorrent.com:80/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://tracker.moeking.me:6969/announce",
    "udp://tracker1.bt.moack.co.kr:80/announce",
    "udp://explodie.org:6969/announce",
    "http://tracker.gbitt.info:80/announce",
};

bool starts_with_ci(const std::string &s, const char *prefix) {
    for (size_t i = 0; prefix[i]; i++) {
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string percent_encode(const std::string &s, bool keep_slash) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (keep_slash && c == '/')) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool is_video_path(const std::string &path) {
    static const char *const kExt[] = {".mkv", ".mp4", ".m4v", ".avi", ".webm", ".mov", ".ts", ".m2ts", ".wmv", ".flv", ".mpg", ".mpeg"};
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const char *e : kExt) {
        const size_t n = std::char_traits<char>::length(e);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, e) == 0) return true;
    }
    return false;
}

}  // namespace

std::string with_trackers(const std::string &link) {
    if (!starts_with_ci(link, "magnet:")) return link;
    std::string out = link;
    for (const char *t : kTrackers) {
        const std::string enc = percent_encode(t, false);
        if (out.find(enc) != std::string::npos || out.find(t) != std::string::npos) continue;
        out += "&tr=" + enc;
    }
    return out;
}

int requested_index(const std::string &link) {
    size_t p = 0;
    while ((p = link.find("index=", p)) != std::string::npos) {
        if (p == 0 || link[p - 1] == '&' || link[p - 1] == '?') {
            size_t e = p + 6;
            int v = 0;
            bool any = false;
            while (e < link.size() && std::isdigit(static_cast<unsigned char>(link[e]))) {
                v = v * 10 + (link[e] - '0');
                any = true;
                e++;
            }
            return any ? v : -1;
        }
        p += 6;
    }
    return -1;
}

const TorrentFile *choose_file(const std::vector<TorrentFile> &files, int requested) {
    if (files.empty()) return nullptr;
    if (requested >= 0) {
        for (const TorrentFile &f : files) {
            if (f.id == requested) return &f;
        }
    }
    const TorrentFile *best = nullptr;
    for (const TorrentFile &f : files) {
        if (is_video_path(f.path) && (!best || f.length > best->length)) best = &f;
    }
    if (best) return best;
    for (const TorrentFile &f : files) {
        if (!best || f.length > best->length) best = &f;
    }
    return best;
}

std::string stream_url(int port, const std::string &hash, const TorrentFile &file) {
    std::string name = file.path;
    const size_t slash = name.rfind('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    return "http://127.0.0.1:" + std::to_string(port) + "/stream/" + percent_encode(name, false) + "?link=" + hash +
           "&index=" + std::to_string(file.id) + "&play";
}

bool parse_status(const std::string &json, std::string *hash, std::vector<TorrentFile> *files, Progress *progress) {
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object()) return false;
    if (hash) *hash = j.value("hash", std::string());
    if (progress) {
        progress->state = j.value("stat_string", std::string());
        progress->active_peers = j.value("active_peers", 0);
        progress->total_peers = j.value("total_peers", 0);
        progress->download_bps = j.value("download_speed", 0.0);
    }
    if (files) {
        files->clear();
        const auto it = j.find("file_stats");
        if (it != j.end() && it->is_array()) {
            for (const auto &f : *it) {
                if (!f.is_object()) continue;
                TorrentFile tf;
                tf.id = f.value("id", 0);
                tf.path = f.value("path", std::string());
                tf.length = f.value("length", int64_t{0});
                if (!tf.path.empty()) files->push_back(std::move(tf));
            }
        }
    }
    return true;
}

TorrentService::TorrentService(std::string binary, std::string work_dir, std::shared_ptr<wnk::net::HttpTransport> transport,
                               wnk::async::WorkerPool &pool, wnk::async::MainDispatcher &dispatcher)
    : binary_(std::move(binary)), work_dir_(std::move(work_dir)), transport_(std::move(transport)), pool_(pool),
      dispatcher_(dispatcher) {}

TorrentService::~TorrentService() { stop(); }

bool TorrentService::available() const { return !binary_.empty() && access(binary_.c_str(), X_OK) == 0 && transport_; }

void TorrentService::stop() {
    std::lock_guard<std::mutex> lock(mu_);
    if (child_) {
        WNK_LOGI(kCat, "torrent: stopping the server");
        child_->kill();
        child_.reset();
    }
    port_ = 0;
}

std::string TorrentService::api(const std::string &path, const std::string &body,
                                const wnk::async::CancellationToken &token, std::string *error) {
    int port;
    {
        std::lock_guard<std::mutex> lock(mu_);
        port = port_;
    }
    wnk::net::HttpRequest req;
    req.url = "http://127.0.0.1:" + std::to_string(port) + path;
    if (!body.empty()) {
        req.method = "POST";
        req.body = body;
        req.headers.emplace_back("Content-Type", "application/json");
    }
    req.timeout_ms = 10000;
    req.connect_timeout_ms = 2000;
    req.idempotent = false;
    const wnk::net::HttpResponse res = transport_->perform(req, token);
    if (!res.ok()) {
        if (error) *error = res.detail.empty() ? "HTTP " + std::to_string(res.status) : res.detail;
        return {};
    }
    return res.body;
}

bool TorrentService::ensure_running(std::string *error) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (child_ && child_->running() && port_ > 0) return true;
    }
    if (!available()) {
        *error = "the torrent server is not installed";
        return false;
    }
    mkdir(work_dir_.c_str(), 0700);
    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (int attempt = 0; attempt < 3; attempt++) {
        const int port = 30000 + static_cast<int>(rng() % 30000);
        core::ChildSpec spec;
        spec.path = binary_;
        // Local only; database and logs on tmpfs. Pieces are cached in RAM (TorrServer's default).
        spec.args = {"--ip", "127.0.0.1", "-p", std::to_string(port), "-d", work_dir_};
        spec.env = {"HOME=" + work_dir_, "TMPDIR=" + work_dir_, "PATH=/usr/bin:/bin"};
        spec.stderr_path = work_dir_ + "/torrserver.log";
        spec.working_dir = work_dir_;
        auto child = std::make_unique<core::ChildProcess>();
        std::string err;
        if (!child->start(spec, [](std::string) {}, [](int) {}, &err)) {
            *error = "could not start the torrent server: " + err;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            child_ = std::move(child);
            port_ = port;
        }
        for (int i = 0; i < 50; i++) {  // up to 5 s for it to answer
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!child_ || !child_->running()) break;
            }
            std::string e;
            if (!api("/echo", "", {}, &e).empty()) {
                WNK_LOGI(kCat, "torrent: server up on 127.0.0.1:%d", port);
                return true;
            }
        }
        WNK_LOGW(kCat, "torrent: server on port %d did not answer; retrying", port);
        stop();
    }
    *error = "the torrent server did not start";
    return false;
}

void TorrentService::resolve(const std::string &link, wnk::async::CancellationToken token,
                             wnk::async::Lifetime::Weak owner, ProgressFn on_progress, DoneFn on_done, int timeout_ms) {
    auto post_progress = [this, owner, on_progress](const Progress &p) {
        dispatcher_.post([owner, on_progress, p]() {
            if (owner.alive() && on_progress) on_progress(p);
        });
    };
    auto finish = [this, owner, on_done](Resolved r) {
        dispatcher_.post([owner, on_done, r = std::move(r)]() {
            if (owner.alive() && on_done) on_done(r);
        });
    };
    const bool queued = pool_.submit([this, link, timeout_ms, post_progress, finish](const wnk::async::CancellationToken &t) {
        Resolved r;
        if (!ensure_running(&r.error)) {
            finish(std::move(r));
            return;
        }
        nlohmann::json add = {{"action", "add"}, {"link", with_trackers(link)}, {"save_to_db", false}};
        std::string err;
        const std::string added = api("/torrents", add.dump(), t, &err);
        std::string hash;
        Progress progress;
        std::vector<TorrentFile> files;
        if (added.empty() || !parse_status(added, &hash, &files, &progress) || hash.empty()) {
            r.error = "the torrent could not be added" + (err.empty() ? std::string() : " (" + err + ")");
            finish(std::move(r));
            return;
        }
        WNK_LOGI(kCat, "torrent: added %s", hash.c_str());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        const nlohmann::json get = {{"action", "get"}, {"hash", hash}};
        while (files.empty()) {
            if (t.cancelled()) return;
            if (std::chrono::steady_clock::now() > deadline) {
                r.error = "no peers sent the torrent's file list in time";
                finish(std::move(r));
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            const std::string st = api("/torrents", get.dump(), t, &err);
            if (!st.empty()) parse_status(st, nullptr, &files, &progress);
            post_progress(progress);
        }
        const TorrentFile *file = choose_file(files, requested_index(link));
        if (!file) {
            r.error = "the torrent has no files";
            finish(std::move(r));
            return;
        }
        int port;
        {
            std::lock_guard<std::mutex> lock(mu_);
            port = port_;
        }
        r.ok = true;
        r.url = stream_url(port, hash, *file);
        r.file = file->path;
        WNK_LOGI(kCat, "torrent: streaming file %d of %zu (%lld MB)", file->id, files.size(),
                 static_cast<long long>(file->length >> 20));
        finish(std::move(r));
    }, token);
    if (!queued) {
        Resolved r;
        r.error = "busy";
        finish(std::move(r));
    }
}

}  // namespace csweb::torrent
