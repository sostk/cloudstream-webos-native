// SPDX-License-Identifier: GPL-3.0-or-later
// plugins/settings_support: settings that need an Android Activity are reported as unsupported.
#define WNK_TEST_MAIN
#include "wnk_test.hpp"

#include "plugins/settings_support.hpp"

using namespace csweb::plugins;

TEST("an Activity cast in openSettings is an unsupported settings screen") {
    // CNCVerse PlayFy v19 (issue #21).
    CHECK(settings_need_activity("null cannot be cast to non-null type androidx.appcompat.app.AppCompatActivity"));
    CHECK(settings_need_activity("org.csweb.compat.HeadlessContext cannot be cast to androidx.fragment.app.FragmentActivity"));
    CHECK(settings_need_activity("org.csweb.compat.HeadlessContext cannot be cast to com.lagradost.cloudstream3.MainActivity"));
    CHECK(settings_failure_message("null cannot be cast to non-null type androidx.appcompat.app.AppCompatActivity")
              .find("cannot show yet") != std::string::npos);
}

TEST("other settings failures keep their message") {
    CHECK(!settings_need_activity("Unable to resolve host \"example.com\""));
    CHECK(!settings_need_activity("android.app.Activity"));  // named, but not a cast or null failure
    CHECK_EQ(settings_failure_message("timeout"), std::string("Settings: timeout"));
    CHECK_EQ(settings_failure_message(""), std::string("Settings: the plugin's settings failed"));
}
