// SPDX-License-Identifier: GPL-3.0-or-later
// The WebView cookie store. There is no WebView in the runtime, so the store is always empty: a plugin
// clearing it (CNC Verse's settings do) succeeds, and reads find no cookies. Cookies of
// the plugin's own HTTP requests live in its HTTP client, not here.
package android.webkit;

public class CookieManager {
    private static final CookieManager INSTANCE = new CookieManager();

    protected CookieManager() {}

    public static CookieManager getInstance() {
        return INSTANCE;
    }

    public String getCookie(String url) {
        return null;
    }

    public void setCookie(String url, String value) {}

    public void setAcceptCookie(boolean accept) {}

    public boolean hasCookies() {
        return false;
    }

    /** Nothing to remove; the callback hears that it is done, as on Android (true: the store is empty). */
    public void removeAllCookies(ValueCallback<Boolean> callback) {
        if (callback != null) callback.onReceiveValue(Boolean.TRUE);
    }

    public void removeSessionCookies(ValueCallback<Boolean> callback) {
        if (callback != null) callback.onReceiveValue(Boolean.TRUE);
    }

    public void flush() {}
}
