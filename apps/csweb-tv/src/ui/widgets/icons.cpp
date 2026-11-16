// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/icons.hpp"

#include "wnk/image/image.hpp"
#include "wnk/log.hpp"
#include "wnk/render/renderer.hpp"
#include "wnk/ui/ui.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace csweb::ui {

namespace {
constexpr const char *kCat = "icons";

std::string &root() {
    static std::string r;
    return r;
}

// An icon rasterised at one size, as a texture of the renderer that made it. Main thread only.
struct Raster {
    wnk::render::Renderer *renderer = nullptr;
    wnk::render::TextureId texture = wnk::render::kNoTexture;
    int width = 0, height = 0;
};

std::map<std::string, Raster> &rasters() {
    static std::map<std::string, Raster> m;
    return m;
}

const Raster &raster(wnk::render::Renderer &r, const std::string &url, int w, int h) {
    const std::string key = url + "|" + std::to_string(w) + "x" + std::to_string(h);
    Raster &out = rasters()[key];
    if (out.renderer == &r) return out;  // made (or failed) before
    out = Raster{&r};
    const std::string path = url.rfind("file://", 0) == 0 ? url.substr(7) : url;
    std::ifstream in(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << in.rdbuf();
    const auto decoded = wnk::image::decode_image(bytes.str(), w, h);
    if (!decoded.image || decoded.image->width <= 0) {
        WNK_LOGW(kCat, "icon %s not drawn: %s", path.c_str(), decoded.detail.c_str());
        return out;
    }
    out.texture = r.create_texture(decoded.image->width, decoded.image->height, decoded.image->pixels.data());
    out.width = decoded.image->width;
    out.height = decoded.image->height;
    return out;
}
}  // namespace

void set_asset_root(const std::string &app_dir) { root() = app_dir; }

std::string icon_url(const std::string &name, bool dark) {
    if (name.empty()) return {};
    return "file://" + root() + "/icons/" + name + (dark && name != "star" ? "-dark" : "") + ".svg";
}

std::string asset_url(const std::string &path) { return "file://" + root() + "/" + path; }

IconView::IconView(std::string url) : url_(std::move(url)) {}

void IconView::draw(wnk::ui::Ui &ui) {
    const wnk::Rect f = frame();
    if (url_.empty() || f.w < 1 || f.h < 1) return;
    // The size an ImageView asks the image service for: the view's, in logical units.
    const Raster &ras = raster(ui.renderer(), url_, static_cast<int>(std::ceil(f.w)), static_cast<int>(std::ceil(f.h)));
    if (ras.texture == wnk::render::kNoTexture) return;
    // Fitted inside the frame, centred (ImageFit::Contain).
    const float ia = static_cast<float>(ras.width) / static_cast<float>(ras.height), fa = f.w / f.h;
    wnk::Rect dst = f;
    if (ia > fa) {
        dst.h = f.w / ia;
        dst.y += (f.h - dst.h) * 0.5f;
    } else {
        dst.w = f.h * ia;
        dst.x += (f.w - dst.w) * 0.5f;
    }
    ui.renderer().draw_image(ras.texture, dst, ui.fade(wnk::render::kWhite));
}

}  // namespace csweb::ui
