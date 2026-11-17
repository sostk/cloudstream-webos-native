// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tool: opens a stream URL through the player's own source (player::make_source, as for a plugin
// link), selects the default tracks as the PlaybackController does, reads a few seconds of packets and
// seeks once. Prints tracks, formats and packet counts; query strings are not printed.
//   csweb-media-probe <url> [VIDEO|M3U8|DASH] [seconds] [seek to, seconds]
#include "player/media_factory.hpp"

#include "wnk/log.hpp"
#include "wnk/media/adaptive_source.hpp"
#include "wnk/net/http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace wnk::media;

namespace {
std::string no_query(const std::string &u) { return u.substr(0, u.find('?')); }

const char *type_name(TrackType t) {
    switch (t) {
        case TrackType::Video: return "video";
        case TrackType::Audio: return "audio";
        case TrackType::Subtitle: return "subtitle";
        default: return "other";
    }
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <url> [VIDEO|M3U8|DASH] [seconds] [seek to]\n", argv[0]);
        return 2;
    }
    // The media pipeline's own log lines (live join, refreshes, retries) on stderr.
    static wnk::log::Logger logger(wnk::log::Level::Info);
    logger.add_sink(std::make_unique<wnk::log::StderrSink>());
    wnk::log::set_default_logger(&logger);
    csweb::runtime::Link link;
    link.url = argv[1];
    link.link_type = argc > 2 ? argv[2] : "VIDEO";
    // CSWEB_PROBE_LINK=<file>: one "link" event line from the runtime (links --withHeaders true), so the
    // link's headers and DRM apply as in the player; <url> and the type argument are then ignored.
    if (const char *path = std::getenv("CSWEB_PROBE_LINK")) {
        std::ifstream in(path);
        std::string line;
        std::getline(in, line);
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            std::fprintf(stderr, "CSWEB_PROBE_LINK: not a JSON link event\n");
            return 2;
        }
        link = csweb::runtime::parse_link(j);
        std::printf("link from %s: %zu header(s), drm %d\n", path, link.headers.size(), link.drm.has_value());
    }
    // CSWEB_PROBE_CLEARKEY="kid:key" (hex or JWK base64url): a ClearKey DrmExtractorLink.
    if (const char *ck = std::getenv("CSWEB_PROBE_CLEARKEY")) {
        const std::string v = ck;
        csweb::runtime::Drm d;
        d.uuid = "e2719d58-a985-b3c9-781a-b030af78d30e";
        d.kid = v.substr(0, v.find(':'));
        d.key = v.find(':') == std::string::npos ? "" : v.substr(v.find(':') + 1);
        link.drm = d;
        std::printf("ClearKey: %zu key(s)\n", csweb::player::clearkey_keys(link).size());
    }
    const double seconds = argc > 3 ? std::stod(argv[3]) : 6;
    std::string err;
    std::shared_ptr<wnk::net::HttpTransport> http = wnk::net::make_curl_transport({}, &err);
    if (!http) {
        std::fprintf(stderr, "http: %s\n", err.c_str());
        return 1;
    }
    DeviceMediaCaps device;
    device.hevc = true;
    device.uhd_panel = true;
    auto src = csweb::player::make_source(link, http, device, 1080);
    std::printf("url %s (%s)\n", no_query(link.url).c_str(), link.link_type.c_str());
    if (!src->open(link.url, &err)) {
        std::printf("RESULT open failed: %s\n", err.c_str());
        return 1;
    }
    const MediaInfo &mi = src->info();
    std::printf("container %s, duration %.1f s, seekable %d, %zu tracks\n", mi.container.c_str(),
                mi.duration_us / 1e6, mi.seekable, mi.tracks.size());
    for (const TrackInfo &t : mi.tracks) {
        std::printf("  track %d %s %s %dx%d %d Hz x%d lang=%s default=%d sub=%d\n", t.index, type_name(t.type),
                    t.codec_label.c_str(), t.width, t.height, t.sample_rate, t.channels, t.language.c_str(),
                    t.is_default, static_cast<int>(t.subtitle));
    }
    if (auto *a = dynamic_cast<AdaptiveSource *>(src.get())) {
        const Presentation &p = a->presentation();
        std::printf("renditions %zu, live %d\n", p.renditions.size(), p.live);
    }
    // Starfish-like capabilities: H.264, HEVC, VP9, AV1; AAC, AC-3, E-AC-3, Opus pass through.
    BackendCaps caps;
    caps.video_codecs = {CodecId::H264, CodecId::Hevc, CodecId::Vp9, CodecId::Av1};
    caps.audio_codecs = {CodecId::Aac, CodecId::Ac3, CodecId::Eac3};
    caps.aac_adts = true;
    caps.adaptive_resolution = true;
    const TrackInfo *v = mi.best(TrackType::Video);
    const TrackInfo *au = mi.best(TrackType::Audio);
    StreamFormat fmt;
    if (!src->select(v ? v->index : -1, au ? au->index : -1, caps, &fmt, &err)) {
        std::printf("RESULT select failed: %s\n", err.c_str());
        return 1;
    }
    if (fmt.video) std::printf("video format %s %dx%d\n", codec_name(fmt.video->codec), fmt.video->width, fmt.video->height);
    if (fmt.audio) std::printf("audio format %s %d Hz x%d\n", codec_name(fmt.audio->codec), fmt.audio->sample_rate, fmt.audio->channels);
    auto read_for = [&](const char *what, int64_t from) {
        int64_t from_us = from;  // kNoTime: from the first packet (live)
        int video = 0, audio = 0, subs = 0;
        int64_t first_v = kNoTime, last_v = kNoTime, last_a = kNoTime;
        int64_t gap_v = 0, gap_a = 0;  // largest jump between consecutive packets (decode order)
        // Wall clock against media time: a live stream that plays steadily delivers media at least as
        // fast as real time once the start-up buffer is used; "behind" is how far delivery lags.
        const auto t0 = std::chrono::steady_clock::now();
        double worst_behind = 0, wall = 0;
        int64_t newest_us = kNoTime;
        for (int i = 0; i < 200000; i++) {
            Packet p;
            const auto r = src->read(&p, &err);
            if (r == MediaSource::Read::EndOfStream) break;
            if (r != MediaSource::Read::Packet) {
                std::printf("RESULT read failed (%s): %s\n", what, err.c_str());
                return false;
            }
            if (from_us == kNoTime && p.pts_us != kNoTime) from_us = p.pts_us;
            wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (p.pts_us != kNoTime && (newest_us == kNoTime || p.pts_us > newest_us)) {
                newest_us = p.pts_us;
                worst_behind = std::max(worst_behind, wall - (newest_us - from_us) / 1e6);
            }
            if (p.type == TrackType::Video) {
                ++video;
                if (first_v == kNoTime) first_v = p.pts_us;
                if (last_v != kNoTime && p.key) gap_v = std::max(gap_v, std::abs(p.pts_us - last_v) > 1000000 ? p.pts_us - last_v : 0);
                last_v = p.pts_us;
            } else if (p.type == TrackType::Audio) {
                ++audio;
                if (last_a != kNoTime) gap_a = std::max(gap_a, std::abs(p.pts_us - last_a));
                last_a = p.pts_us;
            } else {
                ++subs;
            }
            if (last_v != kNoTime && last_v - from_us > seconds * 1e6 && (!fmt.audio || last_a - from_us > seconds * 1e6)) break;
            if (!fmt.video && last_a != kNoTime && last_a - from_us > seconds * 1e6) break;
        }
        std::printf("%s: %d video (%.2f..%.2f s), %d audio (to %.2f s), %d subtitle packets; largest audio step %.3f s%s\n",
                    what, video, first_v / 1e6, last_v / 1e6, audio, last_a / 1e6, subs, gap_a / 1e6,
                    gap_v != 0 ? ", video keyframe jump" : "");
        if (newest_us != kNoTime) {
            std::printf("%s: %.1f s of media in %.1f s wall; delivery at most %.1f s behind real time\n", what,
                        (newest_us - from_us) / 1e6, wall, worst_behind);
        }
        return video > 0 || audio > 0;
    };
    if (!read_for("start", mi.duration_us == kNoTime ? kNoTime : 0)) return 1;
    if (mi.seekable && mi.duration_us > 60000000) {
        const int64_t to = argc > 4 ? static_cast<int64_t>(std::stod(argv[4]) * 1e6) : mi.duration_us / 2;
        if (!src->seek(to, &err)) {
            std::printf("RESULT seek failed: %s\n", err.c_str());
            return 1;
        }
        if (!read_for("after seek", to)) return 1;
    }
    std::printf("RESULT ok\n");
    return 0;
}
