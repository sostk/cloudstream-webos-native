// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/settings_support.hpp"

namespace csweb::plugins {

bool settings_need_activity(const std::string &error) {
    // Kotlin's `x as AppCompatActivity` on null, or a ClassCastException from the headless context
    // to an Activity or fragment type.
    for (const char *type : {"androidx.appcompat.app.AppCompatActivity", "androidx.fragment.app.FragmentActivity",
                             "android.app.Activity", "androidx.fragment.app.FragmentManager",
                             "com.lagradost.cloudstream3.MainActivity"}) {
        if (error.find(type) != std::string::npos &&
            (error.find("cannot be cast") != std::string::npos || error.find("null") != std::string::npos)) {
            return true;
        }
    }
    return false;
}

std::string settings_failure_message(const std::string &error) {
    if (settings_need_activity(error)) {
        return "This plugin's settings are an Android screen, which CSWeb TV cannot show yet. "
               "The plugin itself still works.";
    }
    return "Settings: " + (error.empty() ? std::string("the plugin's settings failed") : error);
}

}  // namespace csweb::plugins
