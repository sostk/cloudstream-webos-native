// SPDX-License-Identifier: GPL-3.0-or-later
// The subset of android.content.Context that headless plugin code uses. Methods keep the Android
// SDK signatures; anything without a headless meaning throws org.csweb.compat.Unsupported.
package android.content;

import java.io.File;

public abstract class Context {
    public static final int MODE_PRIVATE = 0;
    public static final String LAYOUT_INFLATER_SERVICE = "layout_inflater";

    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    public abstract Context getApplicationContext();

    public abstract String getPackageName();

    public abstract File getFilesDir();

    public abstract File getCacheDir();

    public abstract ClassLoader getClassLoader();

    public String getString(int resId) {
        return getResources().getString(resId);
    }

    public final CharSequence getText(int resId) {
        return getResources().getText(resId);
    }

    public android.content.res.Resources getResources() {
        return android.content.res.Resources.getSystem();
    }

    /** The application's assets (org.csweb.compat.AppAssets): cloudstream.jar's, as the APK's on Android. */
    public android.content.res.AssetManager getAssets() {
        return org.csweb.compat.AppAssets.get();
    }

    public android.content.pm.PackageManager getPackageManager() {
        return PACKAGE_MANAGER;
    }

    private static final android.content.pm.PackageManager PACKAGE_MANAGER = new android.content.pm.PackageManager();

    public void startActivity(Intent intent) {
        throw org.csweb.compat.Unsupported.of("Context.startActivity");
    }

    public Object getSystemService(String name) {
        if (LAYOUT_INFLATER_SERVICE.equals(name)) return android.view.LayoutInflater.from(this);
        return null;  // Android returns null for unknown services
    }
}
