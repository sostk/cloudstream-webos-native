// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat;

import android.content.res.AssetManager;

import java.io.File;

/**
 * The application's assets: on Android, an application context's AssetManager reads the app's APK. Here
 * the app is cloudstream.jar on the class path, which keeps the one asset the runtime needs
 * (runtime/make_dist.py KEEP_ASSETS: OkHttp's Public Suffix List). Plugins' own assets come from their
 * archives' AssetManager (PluginManager's addAssetPath), not from here.
 */
public final class AppAssets {
    private static AssetManager assets;

    private AppAssets() {}

    public static synchronized AssetManager get() {
        if (assets == null) {
            AssetManager a = new AssetManager();
            String cp = System.getProperty("java.class.path", "");
            for (String entry : cp.split(File.pathSeparator)) {
                if (new File(entry).getName().equals("cloudstream.jar")) a.addAssetPath(entry);
            }
            assets = a;
        }
        return assets;
    }
}
