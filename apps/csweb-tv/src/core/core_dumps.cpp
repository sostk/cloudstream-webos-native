// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/core_dumps.hpp"

#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>

namespace csweb::core {
namespace {

bool is_dump_name(const char *name) {
    const std::string n = name;
    if (n == "core") return true;
    if (n.rfind("core.", 0) != 0 || n.size() == 5) return false;
    for (size_t i = 5; i < n.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(n[i]))) return false;
    }
    return true;
}

}  // namespace

void disable_core_dumps() {
    struct rlimit none = {0, 0};
    setrlimit(RLIMIT_CORE, &none);
}

int remove_core_dumps(const std::string &dir, uint64_t *bytes) {
    DIR *d = opendir(dir.c_str());
    if (!d) return 0;
    int removed = 0;
    while (const dirent *e = readdir(d)) {
        if (!is_dump_name(e->d_name)) continue;
        const std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (unlink(path.c_str()) == 0) {
            removed++;
            if (bytes) *bytes += static_cast<uint64_t>(st.st_size);
        }
    }
    closedir(d);
    return removed;
}

}  // namespace csweb::core
