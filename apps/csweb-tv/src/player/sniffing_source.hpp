// SPDX-License-Identifier: GPL-3.0-or-later
//
// A VIDEO link that is really an HLS or DASH manifest. Some plugins type a playlist URL without an
// .m3u8/.mpd extension as VIDEO (PlayZTV's "load-playlist-multi"); CloudStream hands it to
// ExoPlayer as video/mp4 and it fails there too. This source opens the link as a file first, so
// ordinary videos cost nothing extra; only when that fails does it fetch the first bytes (with the
// link's headers) and, if the server answers with a playlist content type or the body is one, play
// it through the adaptive source instead. Any other failure keeps the file source's error.
#pragma once

#include "wnk/async/async.hpp"
#include "wnk/media/media.hpp"
#include "wnk/net/http.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace csweb::player {

// True when a response is an HLS or DASH manifest: its Content-Type (any parameters ignored), or a
// body that starts with #EXTM3U or is an XML document whose root is MPD.
bool looks_like_manifest(const std::string &content_type, std::string_view head);

class SniffingSource final : public wnk::media::MediaSource {
 public:
    using Factory = std::function<std::unique_ptr<wnk::media::MediaSource>()>;
    SniffingSource(std::unique_ptr<wnk::media::MediaSource> file, std::shared_ptr<wnk::net::HttpTransport> http,
                   wnk::net::Headers headers, Factory make_adaptive);

    bool open(const std::string &uri, std::string *error) override;
    const wnk::media::MediaInfo &info() const override { return active()->info(); }
    bool select(int video_track, int audio_track, const wnk::media::BackendCaps &caps, wnk::media::StreamFormat *out,
                std::string *error) override {
        return active()->select(video_track, audio_track, caps, out, error);
    }
    Read read(wnk::media::Packet *out, std::string *error) override { return active()->read(out, error); }
    bool seek(int64_t pts_us, std::string *error) override { return active()->seek(pts_us, error); }
    void interrupt() override;
    void reset_interrupt() override;
    bool set_speed(double speed, std::string *why) override { return active()->set_speed(speed, why); }

    // After open(): the link turned out to be a manifest and plays through the adaptive source.
    bool switched() const { return adaptive_ != nullptr; }

 private:
    wnk::media::MediaSource *active() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return adaptive_ ? adaptive_.get() : file_.get();
    }
    bool sniff(const std::string &uri);

    std::unique_ptr<wnk::media::MediaSource> file_;
    std::shared_ptr<wnk::net::HttpTransport> http_;
    wnk::net::Headers headers_;
    Factory make_adaptive_;
    mutable std::mutex mutex_;  // adaptive_ and cancel_
    std::unique_ptr<wnk::media::MediaSource> adaptive_;
    wnk::async::CancellationSource cancel_;
    std::atomic<bool> interrupted_{false};
};

}  // namespace csweb::player
