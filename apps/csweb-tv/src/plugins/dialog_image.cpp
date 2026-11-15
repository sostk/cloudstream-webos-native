// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/dialog_image.hpp"

#include "core/dir_snapshot.hpp"
#include "core/sha256.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace csweb::plugins {

namespace {
bool starts(const std::string &s, const char *prefix, size_t n, size_t at = 0) {
    return s.size() >= at + n && s.compare(at, n, prefix, n) == 0;
}
}  // namespace

std::string dialog_image_extension(const std::string &mime, const std::string &b) {
    if (mime == "image/png" && starts(b, "\x89PNG\r\n\x1a\n", 8)) return "png";
    if (mime == "image/jpeg" && starts(b, "\xff\xd8\xff", 3)) return "jpg";
    if (mime == "image/gif" && (starts(b, "GIF87a", 6) || starts(b, "GIF89a", 6))) return "gif";
    if (mime == "image/webp" && starts(b, "RIFF", 4) && starts(b, "WEBP", 4, 8)) return "webp";
    return {};
}

std::string write_dialog_image(const std::string &dir, const std::string &mime, const std::string &base64,
                               std::string *error) {
    // Base64 is 4/3 of the bytes: refuse before decoding anything far too large.
    if (base64.size() > kDialogImageMaxBytes / 3 * 4 + 4) {
        *error = "image larger than 4 MiB";
        return {};
    }
    std::string bytes;
    if (!core::base64_decode(base64, &bytes) || bytes.empty()) {
        *error = "image data is not base64";
        return {};
    }
    if (bytes.size() > kDialogImageMaxBytes) {
        *error = "image larger than 4 MiB";
        return {};
    }
    const std::string ext = dialog_image_extension(mime, bytes);
    if (ext.empty()) {
        *error = "image is not the " + mime + " it claims to be";
        return {};
    }
    const std::string path = dir + "/" + core::Sha256::hex(bytes) + "." + ext;
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && static_cast<size_t>(st.st_size) == bytes.size()) return path;
    ::mkdir(dir.c_str(), 0700);
    const std::string tmp = path + ".part";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) {
        *error = "cannot write " + tmp;
        return {};
    }
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !written || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        *error = "cannot write " + path;
        return {};
    }
    return path;
}

}  // namespace csweb::plugins
