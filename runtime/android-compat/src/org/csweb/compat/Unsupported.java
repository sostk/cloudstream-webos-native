// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat;

/**
 * Raised when plugin code reaches an Android API that has no meaning in a headless runtime on a webOS
 * TV (views, dialogs, activities, WebView...). The message names the API so compatibility reports can
 * say exactly what a plugin needed.
 */
public final class Unsupported extends UnsupportedOperationException {
    private static final long serialVersionUID = 1L;

    public Unsupported(String api) {
        super("not available on webOS: " + api);
    }

    public static Unsupported of(String api) {
        return new Unsupported(api);
    }

    /**
     * The Android platform class a failure comes from, if it is one the runtime has no counterpart for
     * (ClassNotFoundException "android.webkit.WebView", NoClassDefFoundError "Failed resolution of:
     * Landroid/os/Looper;"); null otherwise. Missing plugin or library classes are not platform features.
     */
    public static String missingPlatformClass(Throwable c) {
        if (!(c instanceof ClassNotFoundException) && !(c instanceof NoClassDefFoundError)) return null;
        String m = String.valueOf(c.getMessage());
        int at = m.indexOf("Landroid/");
        String name;
        if (at >= 0) {
            int end = m.indexOf(';', at);
            name = m.substring(at + 1, end < 0 ? m.length() : end).replace('/', '.');
        } else {
            name = m.trim().replace('/', '.');
        }
        return name.startsWith("android.webkit.") || name.startsWith("android.os.Looper") || name.startsWith("android.os.Handler")
                ? name : null;
    }

    /** Plain words for what a missing platform class stands for (shown to the user). */
    public static String missingFeature(String platformClass) {
        if (platformClass.startsWith("android.webkit.")) return "Android's WebView (a built-in browser)";
        return "Android's main thread (Dispatchers.Main)";
    }
}
