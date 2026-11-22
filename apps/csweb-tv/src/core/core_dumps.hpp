// SPDX-License-Identifier: GPL-3.0-or-later
//
// webOS saves a crashed process's memory as "core.<pid>" in its working directory, which for this app
// is its install folder; with no size limit each one is ~40 MB, and they survive reinstalls (issue
// #36: storage grew far past the package size). The app writes its own crash.log, so dumps are off.
#pragma once

#include <cstdint>
#include <string>

namespace csweb::core {

// No core dumps from this process or the children it starts afterwards (runtime, TorrServer).
void disable_core_dumps();

// Deletes leftover dumps in `dir`: regular files named "core" or "core.<digits>", nothing else.
// Returns how many were removed and adds their size to `*bytes`.
int remove_core_dumps(const std::string &dir, uint64_t *bytes);

}  // namespace csweb::core
