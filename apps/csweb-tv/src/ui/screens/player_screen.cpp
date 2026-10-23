// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/player_screen.hpp"

#include "core/guard.hpp"
#include "core/http_url.hpp"
#include "core/redact.hpp"
#include "player/link_priority.hpp"
#include "player/media_factory.hpp"
#include "settings/preferences.hpp"
#include "ui/theme/theme.hpp"
#include "ui/widgets/pill_button.hpp"

#include "wnk/log.hpp"
#include "wnk/ui/forms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <strings.h>

namespace csweb::ui {

using namespace wnk::ui;
using namespace wnk::media;
using wnk::input::Action;
using wnk::input::Phase;

namespace {
constexpr const char *kCat = "csweb";

std::string clock_text(int64_t us) {
    if (us == kNoTime || us < 0) return "--:--";
    const long long s = us / 1000000;
    char buf[24];
    if (s >= 3600) {
        std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    } else {
        std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
    }
    return buf;
}

// Host only, for logs: a link's URL may carry tokens.
std::string host_of(const std::string &url) {
    const size_t start = url.find("://");
    if (start == std::string::npos) return "?";
    const size_t end = url.find_first_of("/?#", start + 3);
    std::string host = url.substr(start + 3, end == std::string::npos ? std::string::npos : end - start - 3);
    if (const size_t at = host.rfind('@'); at != std::string::npos) host.erase(0, at + 1);
    return host;
}

std::string track_name(const TrackInfo &t) {
    std::string name = !t.title.empty() ? t.title : !t.language.empty() ? t.language : "Track " + std::to_string(t.index);
    if (!t.codec_label.empty()) name += " (" + t.codec_label + ")";
    return name;
}
}  // namespace

PlayerScreen::PlayerScreen(app::Services &s, app::PlayTarget target)
    : Screen(target.title), s_(s), target_(std::move(target)) {
    name = "player";
    subs_view_ = &add<SubtitleView>();
    subs_view_->set_width(Dim::fill()).set_height(Dim::fill());
    subs_view_->set_provider([this]() -> SubtitleView::Cues {
        const int64_t at = position_us_ == kNoTime ? kNoTime : position_us_ - sub_delay_us_;
        if (external_sub_ >= 0) return player::cues_at(external_cues_, at);
        if (!controller_) return {};
        if (sub_delay_us_ == 0 || at == kNoTime) return controller_->subtitles_now();
        const int track = controller_->subtitle_track();
        return track < 0 ? SubtitleView::Cues{} : controller_->subtitle_store().active(track, at);
    });
    const long long size = s_.settings->get_int(settings::kSubtitleSize, 100);
    subs_view_->set_text_scale(0.055f * static_cast<float>(size > 0 && size <= 300 ? size : 100) / 100.0f);
    subs_view_->set_text_style(s_.settings->get_bool(settings::kSubtitleBox, false) ? SubtitleView::TextStyle::Box
                                                                                      : SubtitleView::TextStyle::Outline);

    // Controls: a dark band at the bottom.
    bar_ = &add<Column>(16);
    bar_->set_align(Align::Stretch, Align::End);
    bar_->set_padding({theme::kSafeX, 40, theme::kSafeX, 56});
    bar_->set_background(theme::Color::hex(0x05060AD0));
    auto &top = bar_->add<wnk::ui::Row>(24);
    auto &titles = top.add<Column>(6);
    titles.set_width(Dim::fill());
    auto &title_row = titles.add<wnk::ui::Row>(14);
    title_ = &title_row.add<Label>(target_.title, TextRole::Title);
    title_->set_bold(true);
    bar_badge_row_ = &title_row.add<wnk::ui::Row>(8);
    bar_badge_row_->set_align(Align::Start, Align::Center);
    auto &sub = titles.add<Label>(target_.subtitle);
    sub.set_color(theme::kTextSecondary);
    sub.set_visible(!target_.subtitle.empty());
    status_ = &top.add<Label>("");
    status_->set_muted(true).set_align(Align::End, Align::End);
    progress_ = &bar_->add<ProgressBar>();
    auto &times = bar_->add<wnk::ui::Row>(24);
    time_ = &times.add<Label>("--:-- / --:--");
    time_->set_width(Dim::fill());
    source_ = &times.add<Label>("", TextRole::Small);
    source_->set_muted(true);
    auto &buttons = bar_->add<wnk::ui::Row>(16);
    buttons.set_focus_scope();
    buttons.add<PillButton>("", "replay", PillButton::Style::Glass, [this](Ui &) {
        if (controller_) controller_->seek_relative(-seek_us(false));
    }, 64).accessible_label = "Back";
    pause_ = &buttons.add<PillButton>("", "pause", PillButton::Style::Glass, [this](Ui &) {
        if (controller_) controller_->toggle_pause();
    }, 64);
    pause_->accessible_label = "Play or pause";
    pause_->name = "player.pause";
    buttons.add<PillButton>("", "skip", PillButton::Style::Glass, [this](Ui &) {
        if (controller_) controller_->seek_relative(seek_us(false));
    }, 64).accessible_label = "Forward";
    auto &src = buttons.add<PillButton>("Sources", "play", PillButton::Style::Glass, [this](Ui &ui) { open_sources(ui); }, 64);
    src.name = "player.sources";
    buttons.add<PillButton>("Subtitles", "subtitles", PillButton::Style::Glass, [this](Ui &ui) { open_subtitles(ui); }, 64)
        .name = "player.subtitles";
    buttons.add<PillButton>("Audio", "audio", PillButton::Style::Glass, [this](Ui &ui) { open_audio(ui); }, 64).name =
        "player.audio";
    next_ = &buttons.add<PillButton>("Next episode", "skip", PillButton::Style::Glass, [this](Ui &ui) { play_next(ui); }, 64);
    next_->name = "player.next";
    next_->set_visible(!target_.queue.empty());
    // isThereEpisodes: more than one episode in the list.
    auto &eps = buttons.add<PillButton>("Episodes", "more", PillButton::Style::Glass, [this](Ui &ui) { open_episodes(ui); }, 64);
    eps.name = "player.episodes";
    eps.set_visible(target_.episodes.size() > 1);
    bar_->set_visible(false);

    spinner_ = &add<Spinner>(96);
    spinner_->set_align(Align::Center, Align::Center);
    spinner_->set_visible(false);

    // Starter HUD: shown at top-right on video start and with controls
    starter_hud_ = &add<wnk::ui::Row>();
    starter_hud_->set_width(Dim::fill());
    starter_hud_->set_align(Align::Stretch, Align::Start);
    starter_hud_->set_padding({theme::kSafeX, 48, theme::kSafeX, 0});
    starter_badge_row_ = &starter_hud_->add<wnk::ui::Row>(10);
    starter_badge_row_->set_align(Align::End, Align::Start);
    starter_badge_row_->set_padding({14, 8, 14, 8});
    starter_badge_row_->set_background(theme::Color::hex(0x0C0F17CC), 10.0f);
    starter_hud_->set_visible(false);

    // Loading overlay (CloudStream's link loading screen, with Skip loading).
    auto &loading = add<Column>(26);
    loading_view_ = &loading;
    loading.set_width(Dim::fill()).set_height(Dim::fill());
    loading.set_background(theme::kBackground);
    loading.set_padding({theme::kSafeX, 360, theme::kSafeX, 80});
    auto &lt = loading.add<Label>(target_.title, TextRole::Title);
    lt.set_font_size(theme::kDisplay).set_bold(true).set_max_lines(2);
    loading.add<Label>(target_.subtitle).set_color(theme::kTextSecondary);
    loading_text_ = &loading.add<Label>("Loading links…");
    loading_text_->set_muted(true).set_max_lines(3);
    auto &lrow = loading.add<wnk::ui::Row>(18);
    lrow.add<Spinner>(56);
    skip_ = &lrow.add<PillButton>("Skip loading", "skip", PillButton::Style::Primary, [this](Ui &ui) { start_best(ui); }, 64);
    skip_->name = "player.skip";
    skip_->set_visible(false);
}

PlayerScreen::~PlayerScreen() {
    if (request_ != 0) s_.runtime->cancel(request_);
    save_position();
    stop_playback();
}

void PlayerScreen::on_show(Ui &ui) {
    Screen::on_show(ui);
    if (request_ == 0 && links_.empty() && loading_) load_links();
    if (controller_) ui.set_clear_color(wnk::render::kTransparent);
}

void PlayerScreen::on_hide(Ui &ui) {
    ui.set_clear_color(std::nullopt);
    Screen::on_hide(ui);
}

bool PlayerScreen::on_back(Ui &) {
    save_position();
    stop_playback();
    return false;
}

void PlayerScreen::load_links() {
    loading_ = true;
    request_ = s_.runtime->request(
        {{"op", "links"}, {"provider", target_.provider}, {"url", target_.data}, {"withHeaders", true}},
        core::guarded(life_.weak(), [this](const nlohmann::json &e) {
            const std::string ev = e.value("event", "");
            if (ev == "link") {
                links_.push_back(runtime::parse_link(e));
                const auto &l = links_.back();
                WNK_LOGD(kCat, "link: %s %s %s from %s", l.name.c_str(), runtime::quality_label(l.quality).c_str(),
                         l.link_type.c_str(), host_of(l.url).c_str());
                links_changed();
            } else if (ev == "subtitle") {
                subtitles_.push_back(runtime::parse_subtitle(e));
            }
        }),
        core::guarded(life_.weak(), [this](bool ok, const std::string &error) {
            request_ = 0;
            loading_ = false;
            if (!ok) load_error_ = error;
            WNK_LOGI(kCat, "links loaded: %zu links, %zu subtitles%s%s", links_.size(), subtitles_.size(),
                     ok ? "" : ", error: ", error.c_str());
            // GeneratorPlayer: Success or Failure both start the player (Failure with a toast).
            if (Ui *u = this->ui()) {
                if (!ok && !error.empty() && error != "Cancelled") u->toast(error, 4000);
                if (!started_) start_best(*u);
            }
        }));
}

void PlayerScreen::links_changed() {
    size_t usable = 0;
    bool auto_skip = false;
    const auto prefs = link_preferences();
    for (const auto &l : links_) {
        if (!player::playable(l)) continue;
        ++usable;
        if (player::link_priority(l, prefs) >= player::kAutoSkipPriority) auto_skip = true;
    }
    loading_text_->set_text(links_.empty() ? "Loading links…" : "Loading links… " + std::to_string(links_.size()) + " found");
    if (usable > 0) {
        const bool was_hidden = !skip_->visible();
        skip_->set_text("Skip loading (" + std::to_string(usable) + ")");
        skip_->set_visible(true);
        if (was_hidden) {
            if (Ui *u = this->ui()) u->focus(skip_);
        }
    }
    if (!started_ && auto_skip) {
        if (Ui *u = this->ui()) start_best(*u);
    }
}

void PlayerScreen::start_best(Ui &ui) {
    if (started_) return;
    for (size_t i : player::sort_links(links_, link_preferences())) {
        if (player::playable(links_[i]) && !errored_.count(i)) {
            started_ = true;
            play_link(ui, i);
            return;
        }
    }
    if (!loading_) no_links(links_.empty() ? "No links found" : "No playable links found");
}

void PlayerScreen::no_links(const std::string &why) {
    stop_playback();
    loading_view_->set_visible(true);
    skip_->set_visible(false);
    loading_text_->set_text(why + (load_error_.empty() ? std::string() : " (" + load_error_ + ")") +
                            (links_.empty() ? "" : ". Torrent links cannot be played on the TV."));
    if (Ui *u = this->ui()) u->set_clear_color(std::nullopt);
}

void PlayerScreen::stop_playback() {
    if (controller_) {
        controller_->stop();
        controller_.reset();
    }
    if (screen_on_.held()) screen_on_.release();
    if (starter_hud_) starter_hud_->set_visible(false);
    playback_start_ms_ = 0;
    detected_quality_ = {};
}

void PlayerScreen::play_link(Ui &ui, size_t index) {
    stop_playback();
    current_ = index;
    playback_start_ms_ = 0;
    populate_quality_badges(links_[index], nullptr);
    const runtime::Link &link = links_[index];
    WNK_LOGI(kCat, "playing link %zu: %s %s (%s)", index, link.name.c_str(), runtime::quality_label(link.quality).c_str(),
             player::is_adaptive(link) ? "adaptive" : "ffmpeg");
    tracks_.clear();
    loading_view_->set_visible(false);
    bar_->set_visible(true);
    source_->set_text(link_label(link));
    ui.set_clear_color(wnk::render::kTransparent);
    auto http = s_.player_http ? s_.player_http : s_.ctx->http();
    controller_ = std::make_unique<PlaybackController>(player::make_source(link, http, s_.device,
                                                                           settings::preferred_max_height(*s_.settings)),
                                                       player::make_backend(s_.device),
                                                       s_.ctx->dispatcher(), PlaybackOptions{});
    controller_->set_state_callback(core::guarded(life_.weak(), [this](PlayState from, PlayState to, const PlaybackSnapshot &snap) {
        const std::string error = core::redact_queries(snap.error);
        WNK_LOGI(kCat, "player %s -> %s%s%s", play_state_name(from), play_state_name(to), error.empty() ? "" : ": ",
                 error.c_str());
        if (to == PlayState::Ready) {
            if (playback_start_ms_ == 0 && this->ui()) playback_start_ms_ = this->ui()->now_ms();
            if (current_ < links_.size()) populate_quality_badges(links_[current_], &snap);
            controller_->set_video_area({0, 0, 1920, 1080});
            // GeneratorPlayer.getPos: continue where the viewer stopped (not past 95 %).
            if (!resumed_) {
                resumed_ = true;
                const int64_t start = s_.library->get().start_position_ms(target_.provider, target_.data);
                if (start > 0) {
                    WNK_LOGI(kCat, "player: resuming at %lld s", static_cast<long long>(start / 1000));
                    controller_->seek(start * 1000);
                }
            }
            tracks_ = snap.info.tracks;
            if (external_sub_ < 0) {
                for (const TrackInfo &t : tracks_) {
                    if (t.type == TrackType::Subtitle && t.subtitle != SubtitleKind::None && t.is_default) {
                        controller_->set_subtitle_track(t.index);
                        break;
                    }
                }
            }
            auto_select_subtitles();
        } else if (to == PlayState::Ended) {
            ended_pending_ = true;
        } else if (to == PlayState::Paused) {
            save_position();
        } else if (to == PlayState::Error) {
            // The next link is opened from update(): this controller must not be destroyed inside
            // its own callback.
            errored_.insert(current_);
            next_pending_ = true;
            if (Ui *u = this->ui()) u->toast("This link failed: " + (error.empty() ? std::string("playback error") : error), 3500);
        }
        show_controls();
    }));
    controller_->set_video_area({0, 0, 1920, 1080});
    controller_->open(link.url);
    show_controls();
}

void PlayerScreen::next_mirror(Ui &ui) {
    // GeneratorPlayer.getNextLink: the next link after the current one in sorted order.
    const auto order = player::sort_links(links_, link_preferences());
    auto it = std::find(order.begin(), order.end(), current_);
    for (++it; it < order.end(); ++it) {
        if (player::playable(links_[*it]) && !errored_.count(*it)) {
            play_link(ui, *it);
            return;
        }
    }
    no_links("No more links to try");
}

player::LinkPreferences PlayerScreen::link_preferences() const {
    player::LinkPreferences p;
    p.prefer_dolby_vision = settings::prefer_dolby_vision(*s_.settings);
    p.prefer_dolby_atmos = settings::prefer_dolby_atmos(*s_.settings);
    p.device_dolby_vision = (s_.device.dolby_vision == true);
    p.device_dolby_atmos = (s_.device.dolby_atmos == true);
    return p;
}

std::string PlayerScreen::link_label(const runtime::Link &l) const {
    std::string label = l.name.empty() ? l.source : l.name;
    std::string q = runtime::quality_label(l.quality);
    if (q.empty()) {
        if (player::is_4k(l)) q = "4K";
        else if (player::is_1080p(l)) q = "1080p";
        else if (player::is_720p(l)) q = "720p";
    }
    if (!q.empty()) label += "  ·  " + q;
    if (player::is_dolby_vision(l)) label += "  ·  DV";
    if (player::is_dolby_atmos(l)) label += "  ·  Atmos";
    return label;
}

void PlayerScreen::populate_quality_badges(const runtime::Link &link, const wnk::media::PlaybackSnapshot *snap) {
    bool has_stream_metadata = false;
    int stream_width = 0, stream_height = 0, dv_profile = -1, transfer = 0;
    bool atmos = false;
    if (snap && snap->state >= PlayState::Ready) {
        if (snap->format.video) {
            stream_width = snap->format.video->width;
            stream_height = snap->format.video->height;
            dv_profile = snap->format.video->dolby_vision_profile;
            transfer = snap->format.video->colour.transfer;
            has_stream_metadata = true;
        }
        if (stream_width <= 0 || stream_height <= 0) {
            if (const auto *vt = snap->info.best(wnk::media::TrackType::Video)) {
                stream_width = vt->width;
                stream_height = vt->height;
                has_stream_metadata = true;
            }
        }
        if (snap->format.audio) {
            atmos = snap->format.audio->atmos;
            has_stream_metadata = true;
        }
    }
    auto info = player::detect_quality_info(link, has_stream_metadata, stream_width, stream_height, dv_profile, transfer, atmos);
    if (info.resolution == detected_quality_.resolution &&
        info.dolby_vision == detected_quality_.dolby_vision &&
        info.hdr == detected_quality_.hdr &&
        info.dolby_atmos == detected_quality_.dolby_atmos &&
        starter_badge_row_ && !starter_badge_row_->children().empty()) {
        return;
    }
    detected_quality_ = info;

    auto populate = [&info](wnk::ui::Row *row, float font_size) {
        if (!row) return;
        row->clear_children();
        if (!info.resolution.empty()) {
            auto kind = info.is_4k ? QualityBadge::Kind::Quality4K
                      : info.is_1080p ? QualityBadge::Kind::Quality1080p
                      : info.is_720p ? QualityBadge::Kind::Quality720p
                      : QualityBadge::Kind::QualityGeneric;
            row->add<QualityBadge>(info.resolution, kind, font_size);
        }
        if (info.dolby_vision) {
            row->add<QualityBadge>("VISION", QualityBadge::Kind::DolbyVision, font_size * 0.9f);
        } else if (info.hdr) {
            row->add<QualityBadge>("HDR", QualityBadge::Kind::Hdr, font_size * 0.9f);
        }
        if (info.dolby_atmos) {
            row->add<QualityBadge>("ATMOS", QualityBadge::Kind::DolbyAtmos, font_size * 0.9f);
        }
    };

    populate(starter_badge_row_, 24.0f);
    populate(bar_badge_row_, 20.0f);

    const bool has_any = !info.resolution.empty() || info.dolby_vision || info.hdr || info.dolby_atmos;
    if (starter_hud_) starter_hud_->set_visible(has_any);
    if (bar_badge_row_) bar_badge_row_->set_visible(has_any);
}

void PlayerScreen::show_controls() {
    if (Ui *u = this->ui()) last_input_ms_ = u->now_ms();
}

bool PlayerScreen::on_action(Ui &ui, const wnk::input::ActionEvent &e) {
    if (!controller_ || e.phase == Phase::Release) return false;
    const bool hidden = bar_->opacity() < 0.5f;
    show_controls();
    switch (e.action) {
        case Action::PlayPause:
            if (e.phase == Phase::Press) controller_->toggle_pause();
            return true;
        case Action::Play: controller_->play(); return true;
        case Action::Pause: controller_->pause(); return true;
        case Action::Rewind: controller_->seek_relative(-seek_us(hidden)); return true;
        case Action::FastForward: controller_->seek_relative(seek_us(hidden)); return true;
        case Action::Confirm:
            if (!hidden) return false;
            if (e.phase == Phase::Press) controller_->toggle_pause();
            return true;
        case Action::Left:
            if (!hidden) return false;
            controller_->seek_relative(-seek_us(true));
            return true;
        case Action::Right:
            if (!hidden) return false;
            controller_->seek_relative(seek_us(true));
            return true;
        case Action::Up:
        case Action::Down:
            if (!hidden) return false;
            ui.focus(pause_);
            return true;
        default: return false;
    }
}

void PlayerScreen::update(Ui &ui, float dt) {
    Screen::update(ui, dt);
    if (next_pending_) {
        next_pending_ = false;
        next_mirror(ui);
    }
    if (ended_pending_) {
        ended_pending_ = false;
        if (duration_us_ != kNoTime) position_us_ = duration_us_;
        save_position();
        // PlayerView: the next episode starts by itself only with autoplay_next_key on (the default).
        if (!target_.queue.empty() && s_.settings->get_bool(settings::kAutoplayNext, true)) {
            play_next(ui);
            return;
        }
    }
    if (!controller_) return;
    const PlaybackSnapshot s = controller_->snapshot();
    position_us_ = s.position_us;
    if (s.duration_us > 0) duration_us_ = s.duration_us;
    if (s.state == PlayState::Playing) {
        if (playback_start_ms_ == 0) playback_start_ms_ = ui.now_ms();
        if (ui.now_ms() - last_save_ms_ > 10000) {
            last_save_ms_ = ui.now_ms();
            save_position();
        }
    }
    if (current_ < links_.size() && (detected_quality_.resolution.empty() || (s.format.video && s.format.video->width > 0))) {
        populate_quality_badges(links_[current_], &s);
    }
    const bool busy = s.state == PlayState::Opening || s.state == PlayState::Seeking || s.buffering;
    if (busy || s.state == PlayState::Playing) {
        if (!screen_on_.held()) screen_on_ = s_.ctx->keep_screen_on("video playing");
    } else if (screen_on_.held()) {
        screen_on_.release();
    }
    spinner_->set_visible(busy);
    status_->set_text(s.state == PlayState::Playing ? "" : play_state_name(s.state));
    pause_->set_icon(s.state == PlayState::Paused ? "play" : "pause");
    const bool live = s.duration_us == kNoTime && (s.info.container == "dash" || s.info.container == "hls");
    time_->set_text(clock_text(s.position_us) + " / " + (live ? std::string("LIVE") : clock_text(s.duration_us)));
    if (s.duration_us > 0 && s.position_us != kNoTime) progress_->set_value(static_cast<float>(s.position_us) / s.duration_us);
    const bool keep = s.state != PlayState::Playing || ui.top_dialog() != nullptr;
    const bool visible = keep || ui.now_ms() - last_input_ms_ < 5000;
    bar_->set_opacity(visible ? 1.0f : 0.0f);
    subs_view_->set_bottom_margin(visible ? 0.34f : 0.07f);

    if (starter_hud_ && starter_hud_->visible()) {
        if (playback_start_ms_ > 0) {
            const double elapsed = ui.now_ms() - playback_start_ms_;
            if (elapsed < 4000) {
                starter_hud_->set_opacity(1.0f);
            } else if (elapsed < 5000) {
                const float fade = static_cast<float>(1.0 - (elapsed - 4000) / 1000.0);
                starter_hud_->set_opacity(std::clamp(fade, 0.0f, 1.0f));
            } else {
                const bool controls_shown = bar_->opacity() > 0.5f;
                starter_hud_->set_opacity(controls_shown ? 0.9f : 0.0f);
            }
        } else {
            starter_hud_->set_opacity(1.0f);
        }
    }
}

void PlayerScreen::open_sources(Ui &ui) {
    auto d = std::make_unique<Dialog>("Sources", loading_ ? "Still loading links…" : std::string());
    Dialog *dlg = d.get();
    auto &scroll = d->body().add<ScrollView>();
    scroll.set_height(Dim::fixed(560));
    auto &list = scroll.add<Column>(8);
    View *current = nullptr;
    for (size_t i : player::sort_links(links_, link_preferences())) {
        const auto &l = links_[i];
        std::string text = link_label(l);
        if (!player::playable(l)) text += "  ·  not supported (" + player::unsupported_reason(l) + ")";
        else if (errored_.count(i)) text += "  ·  failed";
        auto &b = list.add<PillButton>(text, i == current_ ? "check" : "", PillButton::Style::Glass, [this, dlg, i](Ui &u) {
            dlg->dismiss(u);
            if (player::playable(links_[i])) {
                errored_.erase(i);
                started_ = true;
                play_link(u, i);
            }
        }, 60);
        b.name = "sources." + std::to_string(i);
        if (i == current_) current = &b;
    }
    d->add_button("Close");
    ui.show_dialog(std::move(d));
    if (current) ui.focus(current);
}

void PlayerScreen::open_subtitles(Ui &ui) {
    auto d = std::make_unique<Dialog>("Subtitles");
    Dialog *dlg = d.get();
    auto &scroll = d->body().add<ScrollView>();
    scroll.set_height(Dim::fixed(560));
    auto &list = scroll.add<Column>(8);
    const int embedded = controller_ ? controller_->subtitle_track() : -1;
    list.add<PillButton>("Off", external_sub_ < 0 && embedded < 0 ? "check" : "", PillButton::Style::Glass, [this, dlg](Ui &u) {
        external_sub_ = -1;
        if (controller_) controller_->set_subtitle_track(-1);
        dlg->dismiss(u);
    }, 60);
    for (size_t i = 0; i < subtitles_.size(); i++) {
        const auto &sub = subtitles_[i];
        list.add<PillButton>(sub.lang.empty() ? "Subtitle " + std::to_string(i + 1) : sub.lang,
                             external_sub_ == static_cast<int>(i) ? "check" : "", PillButton::Style::Glass,
                             [this, dlg, i](Ui &u) {
                                 dlg->dismiss(u);
                                 select_external_subtitle(u, i);
                             }, 60)
            .name = "subs.file." + std::to_string(i);
    }
    for (const TrackInfo &t : tracks_) {
        if (t.type != TrackType::Subtitle || t.subtitle == SubtitleKind::None) continue;
        const int index = t.index;
        list.add<PillButton>(track_name(t), external_sub_ < 0 && embedded == index ? "check" : "", PillButton::Style::Glass,
                             [this, dlg, index](Ui &u) {
                                 external_sub_ = -1;
                                 if (controller_) controller_->set_subtitle_track(index);
                                 dlg->dismiss(u);
                             }, 60)
            .name = "subs.track." + std::to_string(index);
    }
    d->add_button("Sync", [this](Ui &u) { open_subtitle_delay(u); });
    d->add_button("Close");
    ui.show_dialog(std::move(d));
}

void PlayerScreen::select_external_subtitle(Ui &ui, size_t index) {
    if (index >= subtitles_.size() || !s_.http) return;
    wnk::net::HttpRequest req;
    req.url = core::canonical_http_url(subtitles_[index].url);
    req.max_response_bytes = 8u << 20;
    req.timeout_ms = 30000;
    // CS3IPlayer.createOnlineSource(sub.headers): CloudStream's user agent unless the plugin's
    // headers name one, then those headers.
    req.headers.clear();
    bool own_agent = false;
    for (const auto &[k, v] : subtitles_[index].headers) {
        own_agent = own_agent || strcasecmp(k.c_str(), "User-Agent") == 0;
        req.headers.emplace_back(k, v);
    }
    if (!own_agent) req.headers.emplace_back("User-Agent", player::kCloudStreamUserAgent);
    ui.toast("Loading subtitles…", 1500);
    const bool sent = s_.http->send(req, {}, life_.weak(), [this, index](wnk::net::HttpResponse res) {
        Ui *u = this->ui();
        if (!res.ok()) {
            if (u) u->toast("Subtitles could not be downloaded.");
            return;
        }
        std::vector<SubtitleCue> cues;
        std::string error;
        if (!player::parse_subtitle_file(res.body, &cues, &error)) {
            WNK_LOGW(kCat, "subtitle file %zu: %s", index, error.c_str());
            if (u) u->toast("This subtitle format is not supported.");
            return;
        }
        external_cues_.clear();
        for (auto &c : cues) external_cues_.push_back(std::make_shared<const SubtitleCue>(std::move(c)));
        external_sub_ = static_cast<int>(index);
        if (controller_) controller_->set_subtitle_track(-1);
        WNK_LOGI(kCat, "subtitle file %zu: %zu cues", index, external_cues_.size());
    });
    if (!sent) ui.toast("Subtitles could not be downloaded.");
}

void PlayerScreen::auto_select_subtitles() {
    // GeneratorPlayer.autoSelectFromSettings: the first subtitle (by name) in the chosen language,
    // the plugin's files first, then the stream's own tracks. Once per player; "None" turns nothing on.
    if (subs_auto_done_ || !controller_) return;
    subs_auto_done_ = true;
    const std::string tag = settings::subtitle_language(*s_.settings);
    if (tag.empty()) return;
    std::vector<size_t> order(subtitles_.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) { return subtitles_[a].lang < subtitles_[b].lang; });
    for (size_t i : order) {
        if (!settings::subtitle_matches(subtitles_[i].lang, tag)) continue;
        WNK_LOGI(kCat, "subtitles: auto-selecting file %zu (%s)", i, tag.c_str());
        if (Ui *u = this->ui()) select_external_subtitle(*u, i);
        return;
    }
    for (const TrackInfo &t : tracks_) {
        if (t.type != TrackType::Subtitle || t.subtitle == SubtitleKind::None) continue;
        if (settings::subtitle_matches(t.language, tag) || settings::subtitle_matches(t.title, tag)) {
            WNK_LOGI(kCat, "subtitles: auto-selecting track %d (%s)", t.index, tag.c_str());
            controller_->set_subtitle_track(t.index);
            return;
        }
    }
}

void PlayerScreen::open_subtitle_delay(Ui &ui) {
    // CloudStream's subtitle offset: positive shows each line later, negative earlier.
    auto d = std::make_unique<Dialog>("Subtitle sync", "Left and Right move the subtitles by a tenth of a second: positive shows them later, negative earlier.");
    auto &slider = d->body().add<Slider>("Delay", -10.0f, 10.0f, 0.1f, static_cast<float>(sub_delay_us_) / 1e6f,
                                         [this](Ui &, float v) { sub_delay_us_ = std::llround(v * 10.0f) * 100000; });
    slider.set_format([](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%+.1f s", v);
        return std::string(buf);
    });
    slider.set_width(Dim::fixed(900));
    slider.name = "subs.delay";
    Slider *sp = &slider;
    d->add_button("Reset", [this](Ui &) { sub_delay_us_ = 0; });
    d->add_button("Done");
    ui.show_dialog(std::move(d));
    ui.focus(sp);
}

int64_t PlayerScreen::seek_us(bool controls_hidden) const {
    const long long s = s_.settings->get_int(controls_hidden ? settings::kSeekOff : settings::kSeekOn, 10);
    return (s > 0 && s <= 600 ? s : 10) * 1000000LL;
}

void PlayerScreen::open_audio(Ui &ui) {
    if (!controller_) return;
    const PlaybackSnapshot snap = controller_->snapshot();
    auto d = std::make_unique<Dialog>("Audio");
    Dialog *dlg = d.get();
    size_t count = 0;
    for (const TrackInfo &t : tracks_) {
        if (t.type != TrackType::Audio) continue;
        ++count;
        const int index = t.index;
        d->body().add<PillButton>(track_name(t), index == snap.audio_track ? "check" : "", PillButton::Style::Glass,
                                  [this, dlg, index](Ui &u) {
                                      if (controller_) controller_->set_audio_track(index);
                                      dlg->dismiss(u);
                                  }, 60);
    }
    if (count == 0) d->body().add<Label>("No audio tracks to choose from.").set_muted(true);
    d->add_button("Close");
    ui.show_dialog(std::move(d));
}

void PlayerScreen::save_position() {
    // Only real progress of a known length (live streams have none).
    if (!started_ || position_us_ == kNoTime || duration_us_ == kNoTime || duration_us_ <= 0 || position_us_ < 0) return;
    if (target_.show.url.empty()) return;
    library::EpisodeRef cur{target_.data, target_.subtitle, target_.season, target_.episode};
    std::optional<library::EpisodeRef> next;
    if (!target_.queue.empty()) next = target_.queue.front();
    const int64_t pos = position_us_ / 1000, dur = duration_us_ / 1000;
    s_.library->update([&](library::Library &lib) {
        lib.set_view_pos_and_resume(target_.show, cur, next, pos, dur, library::now_ms());
    });
}

void PlayerScreen::play_next(Ui &ui) {
    if (target_.queue.empty()) return;
    // The episode after this one; with the full list known, by position in it.
    if (target_.index + 1 < target_.episodes.size()) {
        WNK_LOGI(kCat, "player: next episode");
        play_episode(ui, target_.index + 1);
        return;
    }
    save_position();
    app::PlayTarget t = target_;
    const library::EpisodeRef n = t.queue.front();
    t.queue.erase(t.queue.begin());
    t.data = n.data;
    t.subtitle = n.name;
    t.season = n.season;
    t.episode = n.episode;
    WNK_LOGI(kCat, "player: next episode");
    s_.router->replace_player(ui, t);
}

void PlayerScreen::play_episode(Ui &ui, size_t index) {
    // GeneratorPlayer.loadThisEpisode: the player starts over with that episode.
    if (index >= target_.episodes.size()) return;
    save_position();
    app::PlayTarget t = target_;
    const library::EpisodeRef &n = t.episodes[index];
    t.index = index;
    t.data = n.data;
    t.subtitle = n.name;
    t.season = n.season;
    t.episode = n.episode;
    t.queue.assign(t.episodes.begin() + static_cast<std::ptrdiff_t>(index) + 1, t.episodes.end());
    s_.router->replace_player(ui, t);
}

void PlayerScreen::open_episodes(Ui &ui) {
    auto d = std::make_unique<Dialog>("Episodes");
    Dialog *dlg = d.get();
    auto &scroll = d->body().add<ScrollView>();
    scroll.set_height(Dim::fixed(600));
    auto &list = scroll.add<Column>(8);
    View *current = nullptr;
    for (size_t i = 0; i < target_.episodes.size(); i++) {
        const auto &e = target_.episodes[i];
        const auto p = s_.library->get().view_pos(target_.provider, e.data);
        std::string text = e.name;
        const int pct = p && p->duration_ms > 0 ? static_cast<int>(library::visual_progress(*p) * 100) : 0;
        if (pct > 0) text += "  ·  " + std::to_string(pct) + " %";
        auto &b = list.add<PillButton>(text, i == target_.index ? "play" : "", PillButton::Style::Glass, [this, dlg, i](Ui &u) {
            dlg->dismiss(u);
            if (i != target_.index) play_episode(u, i);
        }, 60);
        b.name = "episodes." + std::to_string(i);
        if (i == target_.index) current = &b;
    }
    d->add_button("Close");
    ui.show_dialog(std::move(d));
    if (current) ui.focus(current);
}

void PlayerScreen::pause_for_background() {
    save_position();
    if (!controller_) return;
    const PlayState st = controller_->state();
    resume_on_return_ = st == PlayState::Playing || st == PlayState::Seeking;
    controller_->pause_and_release();
}

void PlayerScreen::return_from_background() {
    if (!controller_ || !resume_on_return_) return;
    resume_on_return_ = false;
    controller_->play();
}

}  // namespace csweb::ui
