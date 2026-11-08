// SPDX-License-Identifier: GPL-3.0-or-later
// Type only (AndroidX Fragment implements it); the headless runtime never reports configuration changes.
package android.content;

public interface ComponentCallbacks {
    void onConfigurationChanged(android.content.res.Configuration newConfig);

    void onLowMemory();
}
