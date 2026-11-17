// SPDX-License-Identifier: GPL-3.0-or-later
// player/sniffing_source: a VIDEO link that is really an HLS or DASH manifest.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "player/sniffing_source.hpp"

using namespace csweb::player;
using namespace wnk::media;

namespace {

struct FakeSource : MediaSource {
    bool fail_open = false;
    std::string name, opened;
    int interrupts = 0;
    MediaInfo mi;
    FakeSource(std::string n, bool fail) : fail_open(fail), name(std::move(n)) { mi.container = name; }
    bool open(const std::string &uri, std::string *error) override {
        opened = uri;
        if (fail_open) *error = name + ": Invalid data found when processing input";
        return !fail_open;
    }
    const MediaInfo &info() const override { return mi; }
    bool select(int, int, const BackendCaps &, StreamFormat *, std::string *) override { return true; }
    Read read(Packet *, std::string *) override { return Read::EndOfStream; }
    bool seek(int64_t, std::string *) override { return true; }
    void interrupt() override { ++interrupts; }
    void reset_interrupt() override {}
};

struct FakeHttp : wnk::net::HttpTransport {
    long status = 200;
    std::string content_type, body;
    int requests = 0;
    wnk::net::Headers seen;
    wnk::net::HttpResponse perform(const wnk::net::HttpRequest &req, const wnk::async::CancellationToken &) override {
        ++requests;
        seen = req.headers;
        wnk::net::HttpResponse r;
        r.status = status;
        if (!content_type.empty()) r.headers.push_back({"content-type", content_type});
        r.body = body;
        return r;
    }
    std::string describe() const override { return "fake"; }
};

struct Rig {
    std::shared_ptr<FakeHttp> http = std::make_shared<FakeHttp>();
    FakeSource *file = nullptr, *adaptive = nullptr;
    std::unique_ptr<SniffingSource> src;
    explicit Rig(bool file_fails) {
        auto f = std::make_unique<FakeSource>("file", file_fails);
        file = f.get();
        src = std::make_unique<SniffingSource>(std::move(f), http, wnk::net::Headers{{"Referer", "https://r.example/"}}, [this] {
            auto a = std::make_unique<FakeSource>("adaptive", false);
            adaptive = a.get();
            return std::unique_ptr<MediaSource>(std::move(a));
        });
    }
};

}  // namespace

TEST("looks_like_manifest: by content type or by the first bytes") {
    CHECK(looks_like_manifest("application/vnd.apple.mpegurl", ""));
    CHECK(looks_like_manifest("Application/X-MpegURL; charset=utf-8", ""));
    CHECK(looks_like_manifest("audio/mpegurl", ""));
    CHECK(looks_like_manifest("application/dash+xml", ""));
    CHECK(looks_like_manifest("text/plain", "#EXTM3U\n#EXT-X-VERSION:3\n"));
    CHECK(looks_like_manifest("", "\xEF\xBB\xBF  \r\n#EXTM3U\n"));
    CHECK(looks_like_manifest("application/octet-stream", "<?xml version=\"1.0\"?>\n<!-- x --><MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\">"));
    CHECK(looks_like_manifest("", "<MPD>"));
    CHECK(!looks_like_manifest("video/mp4", std::string("\0\0\0\x18" "ftypmp42", 12)));
    CHECK(!looks_like_manifest("text/html", "<!DOCTYPE html><html>"));
    CHECK(!looks_like_manifest("application/xml", "<?xml version=\"1.0\"?><MPDX/>"));
    CHECK(!looks_like_manifest("", "<?xml version=\"1.0\""));
    CHECK(!looks_like_manifest("", ""));
}

TEST("sniffing source: a file that opens is played as a file, with no extra request") {
    Rig r(false);
    std::string err;
    CHECK(r.src->open("https://h.example/v.mp4", &err));
    CHECK_EQ(r.http->requests, 0);
    CHECK(!r.src->switched());
    CHECK_EQ(r.src->info().container, std::string("file"));
}

TEST("sniffing source: a VIDEO link answering with a playlist plays through the adaptive source") {
    Rig r(true);
    r.http->content_type = "application/vnd.apple.mpegurl";
    r.http->body = "#EXTM3U\n";
    std::string err;
    CHECK(r.src->open("https://h.example/playlist/1/load-playlist-multi", &err));
    CHECK(r.src->switched());
    REQUIRE(r.adaptive != nullptr);
    CHECK_EQ(r.adaptive->opened, std::string("https://h.example/playlist/1/load-playlist-multi"));
    CHECK_EQ(r.src->info().container, std::string("adaptive"));
    CHECK_EQ(r.http->seen.size(), size_t{1});  // the link's headers went with the sniff
    r.src->interrupt();
    CHECK_EQ(r.adaptive->interrupts, 1);
}

TEST("sniffing source: anything else keeps the file source's error") {
    for (auto [status, type, body] : {std::tuple<long, const char *, const char *>{200, "text/html", "<html>"},
                                      {403, "application/vnd.apple.mpegurl", "#EXTM3U"}}) {
        Rig r(true);
        r.http->status = status;
        r.http->content_type = type;
        r.http->body = body;
        std::string err;
        CHECK(!r.src->open("https://h.example/v", &err));
        CHECK(!r.src->switched());
        CHECK_EQ(err, std::string("file: Invalid data found when processing input"));
    }
    Rig local(true);  // not http(s): nothing to fetch
    std::string err;
    CHECK(!local.src->open("/media/v.mkv", &err));
    CHECK_EQ(local.http->requests, 0);
}

TEST("sniffing source: an interrupt during the file open stops before the sniff") {
    Rig r(true);
    r.http->content_type = "application/vnd.apple.mpegurl";
    r.src->interrupt();
    std::string err;
    CHECK(!r.src->open("https://h.example/v", &err));
    CHECK_EQ(r.http->requests, 0);
    r.src->reset_interrupt();
    CHECK(r.src->open("https://h.example/v", &err));
    CHECK(r.src->switched());
}
