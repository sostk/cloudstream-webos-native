// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the viewer is told when a plugin's settings button fails. Some plugins build their settings
// as an Android screen: a Fragment (often a Material BottomSheetDialogFragment) shown through an
// AppCompatActivity's fragment manager, with an inflated layout. The runtime has no Activity window
// or fragment manager to show it on, so openSettings fails with a cast or null error naming the
// Activity type (issue #21: CNCVerse PlayFy, "null cannot be cast to non-null type
// androidx.appcompat.app.AppCompatActivity"). That is an unsupported capability, not a plugin bug,
// and the message says so instead of showing the exception.
#pragma once

#include <string>

namespace csweb::plugins {

// True when the runtime's error says openSettings needed an Android Activity or fragment UI.
bool settings_need_activity(const std::string &error);

// The toast for a failed settings button.
std::string settings_failure_message(const std::string &error);

}  // namespace csweb::plugins
