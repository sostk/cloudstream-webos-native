// SPDX-License-Identifier: GPL-3.0-or-later
// torrent/torrent_service's pure parts: trackers on magnets, the requested file, the file chosen,
// the stream URL and TorrServer's status JSON.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "torrent/torrent_service.hpp"

using namespace csweb::torrent;

TEST("magnets get CloudStream's trackers; other links are left alone") {
    const std::string m = "magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&dn=x";
    const std::string w = with_trackers(m);
    CHECK(w.rfind(m, 0) == 0);
    CHECK(w.find("&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce") != std::string::npos);
    CHECK(with_trackers(w) == w);  // nothing added twice
    CHECK(with_trackers("https://example.org/a.torrent") == "https://example.org/a.torrent");
}

TEST("the index a link asks for") {
    CHECK_EQ(requested_index("magnet:?xt=urn:btih:abc&index=3"), 3);
    CHECK_EQ(requested_index("https://x/a.torrent?index=12&y=1"), 12);
    CHECK_EQ(requested_index("magnet:?xt=urn:btih:abc&dn=reindex=4"), -1);
    CHECK_EQ(requested_index("magnet:?xt=urn:btih:abc"), -1);
}

TEST("the requested file, else the largest video, else the largest file") {
    const std::vector<TorrentFile> files = {
        {1, "Big Buck Bunny/Big Buck Bunny.en.srt", 140},
        {2, "Big Buck Bunny/Big Buck Bunny.mp4", 276134947},
        {3, "Big Buck Bunny/poster.jpg", 310380},
    };
    CHECK_EQ(choose_file(files, -1)->id, 2);
    CHECK_EQ(choose_file(files, 1)->id, 1);
    CHECK_EQ(choose_file(files, 9)->id, 2);  // no such file: the video
    const std::vector<TorrentFile> no_video = {{1, "a.txt", 10}, {2, "b.bin", 20}};
    CHECK_EQ(choose_file(no_video, -1)->id, 2);
    CHECK(choose_file({}, -1) == nullptr);
}

TEST("stream URL: the file name, the hash and TorrServer's 1-based index") {
    const TorrentFile f{2, "Big Buck Bunny/Big Buck Bunny.mp4", 1};
    CHECK_EQ(stream_url(8090, "dd82", f),
             std::string("http://127.0.0.1:8090/stream/Big%20Buck%20Bunny.mp4?link=dd82&index=2&play"));
}

TEST("TorrServer's status JSON") {
    const std::string json = R"({"name":"Big Buck Bunny","hash":"dd82","stat_string":"Torrent working",
        "active_peers":10,"total_peers":510,"download_speed":1600000.5,
        "file_stats":[{"id":1,"path":"B/a.srt","length":140},{"id":2,"path":"B/b.mp4","length":276134947}]})";
    std::string hash;
    std::vector<TorrentFile> files;
    Progress p;
    CHECK(parse_status(json, &hash, &files, &p));
    CHECK_EQ(hash, std::string("dd82"));
    CHECK_EQ(files.size(), size_t{2});
    CHECK_EQ(files[1].length, int64_t{276134947});
    CHECK_EQ(p.active_peers, 10);
    CHECK_EQ(p.total_peers, 510);
    CHECK_EQ(p.state, std::string("Torrent working"));
    CHECK(!parse_status("not json", &hash, &files, &p));
}

TEST("the control bar's torrent line: seeds, peers, speed") {
    Progress p;
    CHECK_EQ(status_text(p), std::string("Looking for peers"));
    p.seeders = 12;
    p.active_peers = 10;
    p.total_peers = 510;
    p.download_bps = 2.5 * 1024 * 1024;
    CHECK_EQ(status_text(p), std::string("12 seeds · 10 of 510 peers · 2.5 MB/s"));
    p.seeders = 1;
    p.download_bps = 300 * 1024;
    CHECK_EQ(status_text(p), std::string("1 seed · 10 of 510 peers · 300 KB/s"));
    const std::string json = R"({"hash":"h","connected_seeders":7,"active_peers":9,"total_peers":40})";
    Progress q;
    CHECK(parse_status(json, nullptr, nullptr, &q));
    CHECK_EQ(q.seeders, 7);
}
