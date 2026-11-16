// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/glass.hpp"

#include "ui/theme/theme.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace csweb::ui::glass {

namespace {
wnk::image::ImageService *g_images = nullptr;
wnk::async::WorkerPool *g_pool = nullptr;
wnk::async::MainDispatcher *g_main = nullptr;

Under g_under;
double g_under_at = -1;  // Ui::now_ms() of the frame it was published in

std::atomic<uint64_t> g_next_id{1};

// One box-blur pass along rows (`horizontal`) or columns, edges clamped.
void box_pass(std::vector<float> &px, int w, int h, int radius, bool horizontal) {
    const int n = horizontal ? w : h, lines = horizontal ? h : w;
    std::vector<float> line(static_cast<size_t>(n) * 3);
    const float scale = 1.0f / static_cast<float>(2 * radius + 1);
    for (int l = 0; l < lines; l++) {
        auto at = [&](int i) -> float * {
            const int x = horizontal ? i : l, y = horizontal ? l : i;
            return &px[(static_cast<size_t>(y) * w + x) * 3];
        };
        for (int i = 0; i < n; i++) std::copy(at(i), at(i) + 3, &line[static_cast<size_t>(i) * 3]);
        float sum[3] = {0, 0, 0};
        for (int k = -radius; k <= radius; k++) {
            const float *p = &line[static_cast<size_t>(std::clamp(k, 0, n - 1)) * 3];
            for (int c = 0; c < 3; c++) sum[c] += p[c];
        }
        for (int i = 0; i < n; i++) {
            float *out = at(i);
            for (int c = 0; c < 3; c++) out[c] = sum[c] * scale;
            const float *drop = &line[static_cast<size_t>(std::clamp(i - radius, 0, n - 1)) * 3];
            const float *take = &line[static_cast<size_t>(std::clamp(i + radius + 1, 0, n - 1)) * 3];
            for (int c = 0; c < 3; c++) sum[c] += take[c] - drop[c];
        }
    }
}

float mixf(float a, float b, float t) { return a + (b - a) * t; }
}  // namespace

Blurred blur(const wnk::image::ImageRGBA &src, int max_width, int radius) {
    Blurred out;
    out.id = g_next_id++;
    if (src.width <= 0 || src.height <= 0 || src.pixels.size() < static_cast<size_t>(src.width) * src.height * 4) return out;
    const int w = std::min(max_width, src.width);
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src.height) * w / src.width)));
    // Area average down to w x h.
    std::vector<float> px(static_cast<size_t>(w) * h * 3, 0.0f);
    for (int y = 0; y < h; y++) {
        const int y0 = y * src.height / h, y1 = std::max(y0 + 1, (y + 1) * src.height / h);
        for (int x = 0; x < w; x++) {
            const int x0 = x * src.width / w, x1 = std::max(x0 + 1, (x + 1) * src.width / w);
            float s[3] = {0, 0, 0};
            for (int sy = y0; sy < y1; sy++) {
                const uint8_t *row = &src.pixels[(static_cast<size_t>(sy) * src.width) * 4];
                for (int sx = x0; sx < x1; sx++) {
                    for (int c = 0; c < 3; c++) s[c] += row[sx * 4 + c];
                }
            }
            const float n = static_cast<float>((y1 - y0) * (x1 - x0));
            for (int c = 0; c < 3; c++) px[(static_cast<size_t>(y) * w + x) * 3 + c] = s[c] / n;
        }
    }
    for (int pass = 0; pass < 3; pass++) {
        box_pass(px, w, h, radius, true);
        box_pass(px, w, h, radius, false);
    }
    out.width = w;
    out.height = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
        for (int c = 0; c < 3; c++) out.rgba[i * 4 + c] = static_cast<uint8_t>(std::clamp(px[i * 3 + c], 0.0f, 255.0f));
        out.rgba[i * 4 + 3] = 255;
    }
    return out;
}

void init(wnk::image::ImageService *images, wnk::async::WorkerPool *pool, wnk::async::MainDispatcher *main) {
    g_images = images;
    g_pool = pool;
    g_main = main;
}

BlurSource::~BlurSource() {
    if (ticket_ != 0 && g_images != nullptr) g_images->cancel(ticket_);
}

void BlurSource::set_url(const std::string &url) {
    if (url == url_) return;
    url_ = url;
    if (ticket_ != 0 && g_images != nullptr) g_images->cancel(ticket_);
    ticket_ = 0;
    if (url.empty()) {
        image_.reset();
        return;
    }
    if (g_images == nullptr || g_pool == nullptr || g_main == nullptr) return;
    // The previous blur stays until the new one is ready: the sharp image is loading meanwhile too.
    ticket_ = g_images->request({url, 320, 320}, life_.weak(), [this, url](wnk::image::ImageResult r) {
        ticket_ = 0;
        if (url != url_ || r.error != wnk::image::ImageError::None || !r.image) return;
        std::shared_ptr<const wnk::image::ImageRGBA> src = r.image;
        wnk::async::run_then_post<std::shared_ptr<const Blurred>>(
            *g_pool, *g_main, life_.weak(), {},
            [src](const wnk::async::CancellationToken &) { return std::make_shared<const Blurred>(blur(*src)); },
            [this, url](std::shared_ptr<const Blurred> b) {
                if (url == url_) image_ = std::move(b);
            });
    });
}

void publish(wnk::ui::Ui &ui, const Under &under) {
    g_under = under;
    g_under_at = ui.now_ms();
}

Style panel_style() {
    Style s;
    s.tint = theme::Color::hex(0x10131AFF);
    return s;
}

Style chip_style() {
    Style s = panel_style();
    s.tint_alpha = 0.42f;
    s.shadow = false;
    return s;
}

Style dialog_style() {
    Style s = panel_style();
    s.tint_alpha = 0.62f;  // text sits on it, and the artwork around is under the scrim
    s.ground_alpha = 0.9f;
    s.resolution = 0.25f;  // a 920 x 1000 panel is 230 x 250 pixels to compose
    return s;
}

std::vector<uint8_t> compose(int w, int h, float radius, const Style &style, const Under *under, const wnk::Rect &at) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 0);
    const theme::Color bg = theme::kBackground;
    const Blurred *img = under && under->image && under->image->width > 0 ? under->image.get() : nullptr;
    // Cover mapping of the backdrop image onto its frame (as ImageView draws it).
    float ox = 0, oy = 0, scale = 1;
    if (img != nullptr) {
        const wnk::Rect &f = under->frame;
        scale = std::max(f.w / img->width, f.h / img->height);
        ox = f.x + (f.w - img->width * scale) * 0.5f;
        oy = f.y + (f.h - img->height * scale) * 0.5f;
    }
    auto sample = [&](float sx, float sy, float *rgb) {
        const float u = std::clamp((sx - ox) / scale - 0.5f, 0.0f, static_cast<float>(img->width - 1));
        const float v = std::clamp((sy - oy) / scale - 0.5f, 0.0f, static_cast<float>(img->height - 1));
        const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
        const int x1 = std::min(x0 + 1, img->width - 1), y1 = std::min(y0 + 1, img->height - 1);
        const float fx = u - x0, fy = v - y0;
        auto px = [&](int x, int y, int c) { return img->rgba[(static_cast<size_t>(y) * img->width + x) * 4 + c] / 255.0f; };
        for (int c = 0; c < 3; c++) {
            rgb[c] = mixf(mixf(px(x0, y0, c), px(x1, y0, c), fx), mixf(px(x0, y1, c), px(x1, y1, c), fx), fy);
        }
    };
    // Texture pixels to logical units (a smaller texture covers the same rectangle).
    const float kx = at.w > 0 ? at.w / w : 1.0f, ky = at.h > 0 ? at.h / h : 1.0f;
    const float bgc[3] = {bg.r, bg.g, bg.b};
    const float tint[3] = {style.tint.r, style.tint.g, style.tint.b};
    const float r = std::min(radius, std::min(w, h) * 0.5f);
    for (int py = 0; py < h; py++) {
        const float cy = py + 0.5f;
        const float k_high = style.highlight * std::max(0.0f, 1.0f - cy / (h * 0.5f));
        const float border_a = mixf(style.border_top, style.border_bottom, cy / h);
        for (int px = 0; px < w; px++) {
            const float cx = px + 0.5f;
            // Signed distance to the rounded rectangle (negative inside).
            const float dx = std::fabs(cx - w * 0.5f) - (w * 0.5f - r);
            const float dy = std::fabs(cy - h * 0.5f) - (h * 0.5f - r);
            const float d = std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f)) + std::min(std::max(dx, dy), 0.0f) - r;
            const float m = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (m <= 0) continue;
            float c[3];
            float alpha;
            if (img != nullptr) {
                const float sx = at.x + cx * kx, sy = at.y + cy * ky;
                sample(sx, sy, c);
                // The backdrop's own scrims, so the glass matches what is around it.
                const wnk::Rect &f = under->frame;
                const float lw = f.w * under->left_extent;
                if (lw > 0 && sx < f.x + lw) {
                    const float a = under->left_strength * (1.0f - std::max(0.0f, (sx - f.x) / lw));
                    for (int i = 0; i < 3; i++) c[i] = mixf(c[i], bgc[i], a);
                }
                const float bh = f.h * under->bottom_extent;
                if (bh > 0 && sy > f.y + f.h - bh) {
                    const float a = std::min(1.0f, (sy - (f.y + f.h - bh)) / bh) * (under->fade_to_background ? 1.0f : 0.8f);
                    for (int i = 0; i < 3; i++) c[i] = mixf(c[i], bgc[i], a);
                }
                // A little more colour, as frosted glass reads, then the tint.
                const float luma = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                for (int i = 0; i < 3; i++) c[i] = mixf(std::clamp(luma + (c[i] - luma) * 1.25f, 0.0f, 1.0f), tint[i], style.tint_alpha);
                alpha = 1;
            } else {
                for (int i = 0; i < 3; i++) c[i] = tint[i];
                alpha = style.ground_alpha;
            }
            for (int i = 0; i < 3; i++) c[i] += (1.0f - c[i]) * k_high;
            // A 1.5 px border just inside the edge.
            const float b = std::clamp(1.5f + d, 0.0f, 1.0f) * border_a;
            for (int i = 0; i < 3; i++) c[i] = mixf(c[i], 1.0f, b);
            alpha = alpha + (1.0f - alpha) * b;
            uint8_t *o = &out[(static_cast<size_t>(py) * w + px) * 4];
            for (int i = 0; i < 3; i++) o[i] = static_cast<uint8_t>(std::lround(std::clamp(c[i], 0.0f, 1.0f) * 255));
            o[3] = static_cast<uint8_t>(std::lround(alpha * m * 255));
        }
    }
    return out;
}

Surface::~Surface() {
    if (renderer_ != nullptr && texture_ != wnk::render::kNoTexture) renderer_->destroy_texture(texture_);
}

void Surface::draw(wnk::ui::Ui &ui, const wnk::Rect &r, float radius, const Style &style, float opacity, wnk::Point shift) {
    auto &rr = ui.renderer();
    const wnk::Rect q{std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h)};
    if (q.w < 2 || q.h < 2 || opacity <= 0) return;
    const wnk::Rect at{q.x + std::round(shift.x), q.y + std::round(shift.y), q.w, q.h};  // where it is drawn
    const Under *under = nullptr;
    if (g_under_at == ui.now_ms() && g_under.image && g_under.image->width > 0) {
        const wnk::Rect &f = g_under.frame;
        if (q.x < f.x + f.w && q.x + q.w > f.x && q.y < f.y + f.h && q.y + q.h > f.y) under = &g_under;
    }
    if (style.shadow) {
        for (int i = 3; i >= 1; i--) {
            const float g = static_cast<float>(i) * 3;
            rr.fill_rect({at.x - g, at.y - g + 6, at.w + g * 2, at.h + g * 2},
                         ui.fade(theme::Color::hex(0x00000000).with_alpha(0.07f * opacity)), radius + g);
        }
    }
    // A texture smaller than the panel (resolution < 1) has its border drawn sharp afterwards instead.
    const float res = std::clamp(style.resolution, 0.05f, 1.0f);
    const bool scaled = res < 1;
    const int tw = std::max(2, static_cast<int>(std::lround(q.w * res)));
    const int th = std::max(2, static_cast<int>(std::lround(q.h * res)));
    std::string key = std::to_string(under ? under->image->id : 0) + ":" + std::to_string(static_cast<int>(q.w)) + "x" +
                      std::to_string(static_cast<int>(q.h)) + ":" + std::to_string(static_cast<int>(radius)) + ":" +
                      std::to_string(style.tint_alpha) + ":" + std::to_string(style.ground_alpha) + ":" +
                      std::to_string(res);
    if (under) {
        // The artwork under it changes with the position (and the backdrop's frame and scrims).
        const wnk::Rect &f = under->frame;
        key += ":" + std::to_string(static_cast<int>(q.x)) + "," + std::to_string(static_cast<int>(q.y)) + ":" +
               std::to_string(static_cast<int>(f.x)) + "," + std::to_string(static_cast<int>(f.y)) + "," +
               std::to_string(static_cast<int>(f.w)) + "," + std::to_string(static_cast<int>(f.h)) + ":" +
               std::to_string(under->left_strength) + std::to_string(under->left_extent) + std::to_string(under->bottom_extent);
    }
    if (key != key_ || texture_ == wnk::render::kNoTexture || renderer_ != &rr) {
        Style s = style;
        if (scaled) s.border_top = s.border_bottom = 0;
        const std::vector<uint8_t> px = compose(tw, th, radius * res, s, under, q);
        if (renderer_ != nullptr && texture_ != wnk::render::kNoTexture) renderer_->destroy_texture(texture_);
        renderer_ = &rr;
        texture_ = rr.create_texture(tw, th, px.data());
        key_ = std::move(key);
    }
    if (texture_ != wnk::render::kNoTexture) rr.draw_image(texture_, at, ui.fade(wnk::render::kWhite.with_alpha(opacity)));
    if (scaled) {
        const float a = (style.border_top + style.border_bottom) * 0.5f * opacity;
        rr.stroke_rect(at, ui.fade(wnk::render::kWhite.with_alpha(a)), 1.5f, radius);
    }
}

}  // namespace csweb::ui::glass
