// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which link plays first, as CloudStream's PlayerGeneratorViewModel.sortLinks orders them with the
// default quality profile (QualityDataHelper.getLinkPriority): the priority of the Qualities entry
// closest to the link's quality (144p 0 … 2160p 8, unknown 4) plus the source priority (1 unless
// the user changed it), highest first, ties in arrival order. A link at AUTO_SKIP_PRIORITY or above
// starts playback before loading ends.
#pragma once

#include "runtime/runtime_types.hpp"

#include <string>
#include <vector>

namespace csweb::player {

inline constexpr int kAutoSkipPriority = 10;
inline constexpr int kDefaultSourcePriority = 1;

struct LinkPreferences {
    bool prefer_dolby_vision = true;
    bool prefer_dolby_atmos = true;
    bool device_dolby_vision = true;
    bool device_dolby_atmos = true;
};

int quality_priority(int quality);
bool is_dolby_vision(const runtime::Link &link);
bool is_dolby_atmos(const runtime::Link &link);
bool is_4k(const runtime::Link &link);
bool is_1080p(const runtime::Link &link);
bool is_720p(const runtime::Link &link);
bool is_hdr(const runtime::Link &link);

struct VideoQualityInfo {
    std::string resolution;
    bool is_4k = false;
    bool is_1080p = false;
    bool is_720p = false;
    bool dolby_vision = false;
    bool hdr = false;
    bool dolby_atmos = false;
};

VideoQualityInfo detect_quality_info(const runtime::Link &link,
                                   bool has_stream_metadata = false,
                                   int stream_width = 0,
                                   int stream_height = 0,
                                   int dv_profile = -1,
                                   int colour_transfer = 0,
                                   bool stream_atmos = false);

int link_priority(const runtime::Link &link, const LinkPreferences &prefs = {});
// Torrents and magnets need CloudStream's torrent server, which the app does not have; anything
// that is not http(s) cannot be opened either. DRM links play only with ClearKey (keys in the clear):
// Widevine and PlayReady need licences for the TV's own protected DRM, which an app cannot use.
bool playable(const runtime::Link &link);
// Why a link cannot play ("" when it can), for the sources list.
std::string unsupported_reason(const runtime::Link &link);
// Indices into `links`, best first.
std::vector<size_t> sort_links(const std::vector<runtime::Link> &links, const LinkPreferences &prefs = {});

// The request headers for a link (ExtractorLink.getAllHeaders: its headers plus Referer when it has
// one and no header names it), with CloudStream's player User-Agent when the link sets none.
std::vector<std::pair<std::string, std::string>> link_headers(const runtime::Link &link);
// MainAPI.USER_AGENT of the CloudStream version the runtime bundles.
extern const char *const kCloudStreamUserAgent;

}  // namespace csweb::player
