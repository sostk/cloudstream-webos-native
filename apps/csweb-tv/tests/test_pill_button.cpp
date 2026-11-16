// SPDX-License-Identifier: GPL-3.0-or-later
// ui/widgets/pill_button: a pill is never wider than its room; long text ends in an ellipsis and a
// suffix (an episode's progress) is kept whole. Its icon is drawn from the first frame, without the
// image service, and rasterised once per icon and size.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "ui/widgets/icons.hpp"
#include "ui/widgets/pill_button.hpp"

#include "wnk/render/font.hpp"
#include "wnk/ui/ui.hpp"

#include <string>
#include <vector>

using namespace wnk;
using namespace wnk::ui;
using csweb::ui::PillButton;

namespace {
// Deterministic metrics: every code point is half the font size wide.
class FakeRenderer : public render::Renderer {
 public:
    std::vector<std::string> texts;
    int textures = 0, images = 0;
    render::CanvasMapping map;
    void set_logical_size(Size) override {}
    void begin_frame(int, int) override { texts.clear(); }
    const render::CanvasMapping &mapping() const override { return map; }
    void clear(render::Color) override {}
    void push_clip(const Rect &) override {}
    void pop_clip() override {}
    void push_translate(float, float) override {}
    void pop_translate() override {}
    void fill_rect(const Rect &, render::Color, float) override {}
    void stroke_rect(const Rect &, render::Color, float, float) override {}
    void draw_image(render::TextureId, const Rect &, render::Color, const Rect &) override { images++; }
    float draw_text(const render::TextStyle &s, Point, std::string_view t) override {
        texts.emplace_back(t);
        return measure_text(s, t);
    }
    float measure_text(const render::TextStyle &s, std::string_view t) const override {
        return static_cast<float>(render::decode_utf8(t).size()) * s.size * 0.5f;
    }
    float line_height(const render::TextStyle &s) const override { return s.size * 1.25f; }
    float ascent(const render::TextStyle &s) const override { return s.size; }
    void end_frame() override {}
    render::TextureId create_texture(int, int, const uint8_t *) override { return ++textures; }
    bool destroy_texture(render::TextureId) override { return true; }
    render::FontId load_font(std::vector<uint8_t>, std::string *) override { return 1; }
    render::FontId load_font_file(const std::string &, std::string *) override { return 1; }
    render::RenderStats stats() const override { return {}; }
    render::GpuInfo gpu_info() const override { return {}; }
};

struct Env {
    FakeRenderer r;
    Ui ui{r, Theme{}};
    void frame() {
        ui.update(1.0f / 60);
        r.begin_frame(1920, 1080);
        ui.draw();
    }
};

bool ends_with(const std::string &s, const std::string &end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}
}  // namespace

TEST("short text keeps its natural width") {
    Env env;
    auto &col = env.ui.root().add<Column>();
    col.set_width(Dim::fixed(800));
    auto &b = col.add<PillButton>("Netflix", "", PillButton::Style::Glass, nullptr, 60);
    env.frame();
    CHECK(b.frame().w < 400);
    CHECK_EQ(env.r.texts.front(), std::string("Netflix"));
}

TEST("long text stays inside its room and ends in an ellipsis, the suffix whole") {
    Env env;
    auto &col = env.ui.root().add<Column>();
    col.set_width(Dim::fixed(400));
    const std::string title(80, 'x');  // far wider than 400 at any text size
    auto &b = col.add<PillButton>(title, "", PillButton::Style::Glass, nullptr, 60);
    b.set_suffix("  ·  100 %");
    env.frame();
    CHECK(b.frame().w <= 400);
    CHECK_EQ(env.r.texts.size(), size_t{2});
    CHECK(env.r.texts[0].size() < title.size());
    CHECK(ends_with(env.r.texts[0], "\xE2\x80\xA6"));  // …
    CHECK_EQ(env.r.texts[1], std::string("  ·  100 %"));
}

TEST("icons draw on the first frame and are rasterised once per size") {
    csweb::ui::set_asset_root(CSWEB_ASSETS_DIR);  // the app's assets/ in the source tree
    Env env;  // no image service: Ui::images() is null, as before the app's services start
    auto &col = env.ui.root().add<Column>(8);
    col.set_width(Dim::fixed(800));
    col.add<PillButton>("Sources", "play", PillButton::Style::Glass, nullptr, 60);
    col.add<PillButton>("Trailer", "play", PillButton::Style::Glass, nullptr, 60);
    env.frame();
    CHECK_EQ(env.r.images, 2);    // both icons on screen at once
    CHECK_EQ(env.r.textures, 1);  // one texture for the same icon at the same size
    env.frame();
    CHECK_EQ(env.r.textures, 1);  // and kept
}
