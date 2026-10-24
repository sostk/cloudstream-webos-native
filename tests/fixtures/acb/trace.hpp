// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
inline bool flag(const char *name) { return std::getenv(name) != nullptr; }
inline void trace(const char *format, ...) {
    FILE *f = std::fopen(std::getenv("ACB_TEST_TRACE"), "a");
    assert(f);
    va_list args;
    va_start(args, format);
    std::vfprintf(f, format, args);
    va_end(args);
    std::fputc('\n', f);
    std::fclose(f);
}
