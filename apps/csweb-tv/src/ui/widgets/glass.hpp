// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted glass over artwork. The renderer has no backdrop blur and no rounded clip, so a glass
// panel is a texture composed on the CPU: the part of the backdrop under the panel, taken from a
// small blurred copy of the artwork (and darkened by the backdrop's own scrims), tinted, cut to the
// panel's rounded shape, with a border that is brighter at the top and a faint top highlight. A
// panel over no artwork (a plain screen, video) gets the same tint, border and highlight over a
// translucent ground.
//
// The Backdrop that is drawn publishes its blurred copy for the frame (publish()); glass drawn later
// in the same frame (the top bar, hero buttons) uses it. A panel's texture is composed again only
// when the artwork, its rectangle or its look changes.
#pragma once

#include "wnk/async/async.hpp"
#include "wnk/image/image.hpp"
#include "wnk/render/renderer.hpp"
#include "wnk/ui/ui.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace csweb::ui::glass {

// A blurred, small copy of an artwork image (straight-alpha RGBA, top row first).
struct Blurred {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    uint64_t id = 0;  // unique per image
};

// Box-blurs (three passes, an approximate Gaussian) a copy of `src` scaled to at most `max_width`.
Blurred blur(const wnk::image::ImageRGBA &src, int max_width = 160, int radius = 3);

// Called once at start (the app's image service, pool and main dispatcher).
void init(wnk::image::ImageService *images, wnk::async::WorkerPool *pool, wnk::async::MainDispatcher *main);

// Loads and blurs a Backdrop's image off the main thread.
class BlurSource {
 public:
    ~BlurSource();
    void set_url(const std::string &url);
    const std::shared_ptr<const Blurred> &image() const { return image_; }

 private:
    std::string url_;
    std::shared_ptr<const Blurred> image_;
    wnk::image::ImageTicket ticket_ = 0;
    wnk::async::Lifetime life_;
};

// What a drawn Backdrop shows: its frame (image fitted with Cover) and its scrims.
struct Under {
    std::shared_ptr<const Blurred> image;
    wnk::Rect frame;
    float left_strength = 0, left_extent = 0, bottom_extent = 0;
    bool fade_to_background = true;
};
void publish(wnk::ui::Ui &ui, const Under &under);

// How far the scrolling views above `v` move what it draws: added to a rectangle in its layout
// coordinates, it gives the one Backdrops and panels are matched in (slides are left out).
wnk::Point scroll_shift(const wnk::ui::View &v);

// The look of a glass panel.
struct Style {
    wnk::render::Color tint;  // over the blurred artwork
    float tint_alpha = 0.5f;
    float ground_alpha = 0.86f;   // overall opacity when there is no artwork under the panel
    float border_top = 0.30f, border_bottom = 0.07f;  // white border alpha, top to bottom
    float highlight = 0.07f;      // white at the top edge, fading out by the middle
    bool shadow = true;
    // Texture pixels per logical unit. Below 1 (large panels: dialogs) the texture is composed smaller
    // and scaled up, which the blur hides, and the border is drawn sharp over it instead.
    float resolution = 1;
};
Style panel_style();   // nav bar, pills over artwork
Style chip_style();    // small pills: lighter shadow
Style dialog_style();  // dialog panels: denser tint for text, a quarter-resolution texture

// One glass panel's texture. Owned by the widget that draws it.
class Surface {
 public:
    Surface() = default;
    ~Surface();
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    // Draws the panel at `r` (logical units) with corner `radius`, at `opacity` and moved by `shift`.
    // The artwork under it is that under `r` moved by `scrolled` (scroll_shift() of the panel's view):
    // a panel sliding in (shift) is not composed again, nor one scrolling along with its artwork.
    void draw(wnk::ui::Ui &ui, const wnk::Rect &r, float radius, const Style &style, float opacity = 1,
              wnk::Point shift = {}, wnk::Point scrolled = {});

 private:
    wnk::render::Renderer *renderer_ = nullptr;
    wnk::render::TextureId texture_ = wnk::render::kNoTexture;
    std::string key_;
};

// Composes a panel's pixels (exposed for tests): w x h straight-alpha RGBA covering the logical
// rectangle `at` (w x h pixels may be fewer than at.w x at.h; `radius` is in pixels).
std::vector<uint8_t> compose(int w, int h, float radius, const Style &style, const Under *under, const wnk::Rect &at);

}  // namespace csweb::ui::glass
