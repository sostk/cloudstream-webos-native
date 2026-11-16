// SPDX-License-Identifier: GPL-3.0-or-later
// plugins/dialog_image: images in plugin dialogs (a login QR code, issue #38) are checked and written once.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "core/dir_snapshot.hpp"
#include "plugins/dialog_image.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace csweb::plugins;
using csweb::core::base64_encode;

namespace {
std::string tmpdir() {
    char t[] = "/tmp/csweb-dlgimg-XXXXXX";
    return ::mkdtemp(t);
}
std::string read(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
const std::string kPng = std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\x04\0\0\0\x04", 24);
}  // namespace

TEST("image types are recognised by their bytes") {
    CHECK_EQ(dialog_image_extension("image/png", kPng), std::string("png"));
    CHECK_EQ(dialog_image_extension("image/jpeg", std::string("\xff\xd8\xff\xe0", 4)), std::string("jpg"));
    CHECK_EQ(dialog_image_extension("image/gif", "GIF89a.."), std::string("gif"));
    CHECK_EQ(dialog_image_extension("image/webp", std::string("RIFF\x10\0\0\0WEBPVP8 ", 16)), std::string("webp"));
    CHECK(dialog_image_extension("image/jpeg", kPng).empty());  // not what it claims
    CHECK(dialog_image_extension("image/svg+xml", "<svg/>").empty());
    CHECK(dialog_image_extension("image/png", "").empty());
}

TEST("written once under its hash") {
    const std::string dir = tmpdir() + "/images";  // created on first use
    std::string error;
    const std::string path = write_dialog_image(dir, "image/png", base64_encode(kPng), &error);
    CHECK(error.empty());
    CHECK(path.rfind(dir + "/", 0) == 0);
    CHECK(path.size() == dir.size() + 1 + 64 + 4);
    CHECK_EQ(path.substr(path.size() - 4), std::string(".png"));
    CHECK_EQ(read(path), kPng);
    CHECK_EQ(write_dialog_image(dir, "image/png", base64_encode(kPng), &error), path);  // reused
}

TEST("bad image data is refused") {
    const std::string dir = tmpdir();
    std::string error;
    CHECK(write_dialog_image(dir, "image/png", "not base64!", &error).empty());
    CHECK(!error.empty());
    error.clear();
    CHECK(write_dialog_image(dir, "image/gif", base64_encode(kPng), &error).empty());
    CHECK(error.find("image/gif") != std::string::npos);
    error.clear();
    const std::string huge(kDialogImageMaxBytes + 1, 'A');
    CHECK(write_dialog_image(dir, "image/png", base64_encode(kPng + huge), &error).empty());
    CHECK(error.find("4 MiB") != std::string::npos);
}
