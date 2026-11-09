// SPDX-License-Identifier: GPL-3.0-or-later
// The package manager of a headless runtime: there is no launcher and no other app to start. Plugins
// that "restart the app" (getLaunchIntentForPackage, then startActivity and Runtime.exit) get no
// intent and so skip the restart; the native app reloads a plugin after its settings close instead.
package android.content.pm;

import android.content.Intent;

public class PackageManager {
    public static final int GET_META_DATA = 0x80;

    /** No launchable activity on webOS: always null (Android returns null for such packages too). */
    public Intent getLaunchIntentForPackage(String packageName) {
        return null;
    }
}
