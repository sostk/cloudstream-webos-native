// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the viewer is told when a plugin's settings button fails. Some plugins build their settings
// as an Android screen shown through an Activity. The runtime shows Material bottom sheets opened
// from the headless AppCompatActivity (docs/runtime/dialog-protocol.md; CNCVerse PlayFy, issue #21),
// but other Activity UI (fragment transactions, activities started for a result, custom windows)
// fails with a cast or null error naming the Activity type, e.g. "null cannot be cast to non-null
// type androidx.appcompat.app.AppCompatActivity". That is an unsupported capability, not a plugin
// bug, and the message says so instead of showing the exception.
#pragma once

#include <string>

namespace csweb::plugins {

// True when the runtime's error says openSettings needed an Android Activity or fragment UI.
bool settings_need_activity(const std::string &error);

// The toast for a failed settings button.
std::string settings_failure_message(const std::string &error);

}  // namespace csweb::plugins
