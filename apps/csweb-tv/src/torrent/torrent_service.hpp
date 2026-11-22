// SPDX-License-Identifier: GPL-3.0-or-later
//
// Torrent and magnet links, played as CloudStream does (app/.../ui/player/Torrent.kt): a TorrServer
// (https://github.com/YouROK/TorrServer, GPL-3.0; tools/build-torrserver.sh) runs beside the app,
// the link is added to it, and the player gets the server's HTTP stream of the chosen file. The
// server listens on 127.0.0.1 only, keeps its database on the app's tmpfs and caches pieces in RAM
// (no writes to the TV's storage). It starts with the first torrent and stops with the app.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <wnk/async/async.hpp>
#include <wnk/net/http.hpp>

#include "core/child_process.hpp"

namespace csweb::torrent {

struct TorrentFile {
    int id = 0;  // TorrServer's file index (1-based)
    std::string path;
    int64_t length = 0;
};

// What the player shows while a torrent starts.
struct Progress {
    std::string state;  // TorrServer's stat_string ("Torrent getting info", "Torrent working", ...)
    int active_peers = 0, total_peers = 0;
    double download_bps = 0;
};

struct Resolved {
    bool ok = false;
    std::string error;
    std::string url;   // http://127.0.0.1:<port>/stream/<name>?link=<hash>&index=<n>&play
    std::string file;  // the chosen file's path in the torrent
};

// ---- Pure helpers (tests/test_torrent.cpp) ----

// CloudStream's tracker list (Torrent.kt), appended to a magnet as &tr= so a magnet without trackers
// still finds peers. Other links (a .torrent URL, a bare hash) are returned unchanged.
std::string with_trackers(const std::string &link);
// The `index=` CloudStream links may carry (Torrent.kt streamUrl): which file to play; -1 if none.
int requested_index(const std::string &link);
// The file to stream: the requested index when it names a file, else the largest video file, else
// the largest file. nullptr when there are none.
const TorrentFile *choose_file(const std::vector<TorrentFile> &files, int requested);
std::string stream_url(int port, const std::string &hash, const TorrentFile &file);
// TorrServer's /torrents "get"/"add" answer: hash, files, progress. False when it is not JSON.
bool parse_status(const std::string &json, std::string *hash, std::vector<TorrentFile> *files, Progress *progress);

class TorrentService {
 public:
    // `binary`: the bundled TorrServer; `work_dir`: a tmpfs directory for its database and log.
    TorrentService(std::string binary, std::string work_dir, std::shared_ptr<wnk::net::HttpTransport> transport,
                   wnk::async::WorkerPool &pool, wnk::async::MainDispatcher &dispatcher);
    ~TorrentService();  // stops the server

    bool available() const;  // the binary is installed

    using ProgressFn = std::function<void(const Progress &)>;
    using DoneFn = std::function<void(const Resolved &)>;
    // Starts the server if needed, adds `link` and waits (up to `timeout_ms`) for its file list.
    // Callbacks run on the main thread and only while `owner` is alive. Cancel with `token`.
    void resolve(const std::string &link, wnk::async::CancellationToken token, wnk::async::Lifetime::Weak owner,
                 ProgressFn on_progress, DoneFn on_done, int timeout_ms = 60000);

    void stop();

 private:
    bool ensure_running(std::string *error);  // on a worker
    std::string api(const std::string &path, const std::string &body, const wnk::async::CancellationToken &token,
                    std::string *error);

    const std::string binary_, work_dir_;
    std::shared_ptr<wnk::net::HttpTransport> transport_;
    wnk::async::WorkerPool &pool_;
    wnk::async::MainDispatcher &dispatcher_;
    std::mutex mu_;  // guards child_ and port_
    std::unique_ptr<core::ChildProcess> child_;
    int port_ = 0;
};

}  // namespace csweb::torrent
