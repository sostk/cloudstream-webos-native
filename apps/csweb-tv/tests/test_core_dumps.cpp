// SPDX-License-Identifier: GPL-3.0-or-later
// core/core_dumps: leftover crash dumps are removed, nothing else is (issue #36).
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "core/core_dumps.hpp"

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void write(const std::string &path, size_t size) {
    FILE *f = std::fopen(path.c_str(), "wb");
    for (size_t i = 0; i < size; i++) std::fputc('x', f);
    std::fclose(f);
}
bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }
}  // namespace

TEST("only core and core.<digits> regular files go") {
    char tmpl[] = "/tmp/csweb-core-XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    write(dir + "/core.12375", 100);
    write(dir + "/core", 50);
    write(dir + "/core.txt", 10);
    write(dir + "/corefile", 10);
    write(dir + "/csweb-tv", 10);
    mkdir((dir + "/core.99").c_str(), 0700);  // a directory with a dump's name stays
    uint64_t bytes = 0;
    CHECK_EQ(csweb::core::remove_core_dumps(dir, &bytes), 2);
    CHECK_EQ(bytes, uint64_t{150});
    CHECK(!exists(dir + "/core.12375"));
    CHECK(!exists(dir + "/core"));
    CHECK(exists(dir + "/core.txt"));
    CHECK(exists(dir + "/corefile"));
    CHECK(exists(dir + "/csweb-tv"));
    CHECK(exists(dir + "/core.99"));
    CHECK_EQ(csweb::core::remove_core_dumps(dir + "/missing", &bytes), 0);
    std::system(("rm -rf '" + dir + "'").c_str());
}

TEST("dumps are switched off") {
    csweb::core::disable_core_dumps();
    struct rlimit r;
    REQUIRE(getrlimit(RLIMIT_CORE, &r) == 0);
    CHECK_EQ(r.rlim_cur, rlim_t{0});
}
