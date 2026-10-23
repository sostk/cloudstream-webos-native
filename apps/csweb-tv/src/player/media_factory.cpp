// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/media_factory.hpp"

#include "player/link_priority.hpp"
#include "player/merged_source.hpp"

#include "wnk/log.hpp"
#include "wnk/media/adaptive_source.hpp"
#include "wnk/media/auto_backend.hpp"
#include "wnk/media/clearkey.hpp"
#include "wnk/media/ffmpeg_source.hpp"
#include "wnk/media/ndl_backend.hpp"
#include "wnk/media/pacing_backend.hpp"
#if defined(CSWEB_LEGACY) && defined(CSWEB_LEGACY_VIDEO_PLANE)
#include "player/legacy_starfish_backend.hpp"
#else
#include "wnk/media/starfish_backend.hpp"
#endif
#include "wnk/platform/process.hpp"

namespace csweb::player {

using namespace wnk::media;
#if defined(CSWEB_LEGACY) && defined(CSWEB_LEGACY_VIDEO_PLANE)
using StarfishBackend = wnk::media::LegacyStarfishBackend;
using StarfishOptions = wnk::media::LegacyStarfishOptions;
#endif

std::unique_ptr<MediaBackend> make_backend(const DeviceMediaCaps &device) {
    if (!wnk::platform::kIsWebOS) return std::make_unique<PacingBackend>();
    std::string why_sf, why_ndl;
    const bool starfish = StarfishBackend::available(&why_sf);
    const bool ndl = NdlBackend::available(&why_ndl);
    StarfishOptions so;
#if defined(CSWEB_LEGACY) && defined(CSWEB_LEGACY_VIDEO_PLANE)
    so.app_id = "org.csweb.tv.legacy";  // the id this package registers with SAM
#endif
    so.dolby_vision = device.dolby_vision == true;
    so.atmos = device.dolby_atmos == true;
    if (starfish && ndl) return std::make_unique<AutoBackend>(std::make_unique<StarfishBackend>(so), std::make_unique<NdlBackend>());
    if (starfish) return std::make_unique<StarfishBackend>(so);
    if (ndl) return std::make_unique<NdlBackend>();
    WNK_LOGW("csweb", "no media backend (Starfish: %s; NDL: %s)", why_sf.c_str(), why_ndl.c_str());
    return std::make_unique<PacingBackend>();
}

bool is_adaptive(const runtime::Link &link) {
    return link.link_type == "M3U8" || link.link_type == "DASH" || AdaptiveSource::handles(link.url);
}

ContentKeys clearkey_keys(const runtime::Link &link) {
    // CS3IPlayer: {"keys":[{"kty":..,"k":key,"kid":kid}]} for ExoPlayer's ClearKey session; the kit
    // takes the same JWK values (or hex).
    ContentKeys keys;
    if (!link.drm || !link.drm->clearkey() || link.drm->kid.empty() || link.drm->key.empty()) return keys;
    const std::string kid = normalize_key_id(link.drm->kid), key = normalize_key_id(link.drm->key);
    if (kid.empty() || key.empty()) {
        WNK_LOGW("csweb", "ClearKey link with a malformed key id or key");
        return keys;
    }
    keys[kid] = key;
    return keys;
}

std::unique_ptr<MediaSource> make_source(const runtime::Link &link, std::shared_ptr<wnk::net::HttpTransport> http,
                                         const DeviceMediaCaps &device, int max_height) {
    wnk::net::Headers headers;
    for (auto &h : link_headers(link)) headers.push_back(std::move(h));
    const ContentKeys keys = clearkey_keys(link);
    if (is_adaptive(link)) {
        AdaptiveSourceOptions o;
        o.keys = keys;
        if (link.drm && link.drm->clearkey()) {
            o.license_url = link.drm->license_url;  // asked only for key IDs the link gave no key for
            o.license_headers = headers;
        }
        o.http = std::move(http);
        o.headers = std::move(headers);
        // ExoPlayer plays whatever the device decodes; the TV's decoders take VP9 and AV1 too (WebM
        // DASH, YouTube manifests). HEVC and H.264 stay preferred at equal picture size.
        o.video_codecs = {CodecId::Hevc, CodecId::H264, CodecId::Vp9, CodecId::Av1};
        if (device.hevc == false) o.video_codecs.erase(o.video_codecs.begin());
        if (device.uhd_panel != true) o.max_height = 1080;
        if (max_height > 0 && max_height < o.max_height) o.max_height = max_height;
        o.dolby_vision = device.dolby_vision == true;
        return std::make_unique<AdaptiveSource>(o);
    }
    FfmpegSourceOptions o;
    o.http = http;
    o.http_stream.headers = std::move(headers);
    o.decryption_keys = keys;  // Common Encryption in a single MP4 file
    auto video = std::make_unique<FfmpegSource>(std::move(o));
    if (link.audio_tracks.empty()) return video;
    // CS3IPlayer.getAudioSources: each AudioFile with its own headers (CloudStream's user agent unless
    // they name one), merged with the video.
    std::vector<std::string> uris;
    std::vector<std::unique_ptr<MediaSource>> audio;
    for (const runtime::AudioTrack &a : link.audio_tracks) {
        runtime::Link only;
        only.headers = a.headers;
        FfmpegSourceOptions ao;
        ao.http = http;
        ao.decryption_keys = keys;
        for (auto &h : link_headers(only)) ao.http_stream.headers.push_back(std::move(h));
        uris.push_back(a.url);
        audio.push_back(std::make_unique<FfmpegSource>(std::move(ao)));
    }
    return std::make_unique<MergedSource>(std::move(video), std::move(uris), std::move(audio));
}

}  // namespace csweb::player
