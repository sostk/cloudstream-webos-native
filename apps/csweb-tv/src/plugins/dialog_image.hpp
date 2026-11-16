// SPDX-License-Identifier: GPL-3.0-or-later
//
// Images in plugin dialogs (docs/runtime/dialog-protocol.md, view type "image"): a bitmap the plugin
// made, such as Kartoons' login QR code (issue #38), arrives as base64 of its encoded bytes. The app
// checks them and writes them to a file the kit's ImageView loads (file://), named by their SHA-256 so
// a dialog described again reuses the file.
#pragma once

#include <cstddef>
#include <string>

namespace csweb::plugins {

// Decoded images larger than this are refused.
inline constexpr size_t kDialogImageMaxBytes = 4u << 20;

// "png", "jpg", "gif" or "webp" when `bytes` start like an image of type `mime` ("image/png",
// "image/jpeg", "image/gif", "image/webp"); empty otherwise.
std::string dialog_image_extension(const std::string &mime, const std::string &bytes);

// Writes the image to `<dir>/<sha256>.<ext>` (not again if it is there) and returns that path; empty with
// *error set when the data is not base64, too large, not the type it claims, or cannot be written.
std::string write_dialog_image(const std::string &dir, const std::string &mime, const std::string &base64,
                               std::string *error);

}  // namespace csweb::plugins
