// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat.ui;

import org.csweb.compat.Unsupported;

/**
 * Where plugin UI goes. The runtime has no window system: a host (the loader) receives dialogs and toasts
 * and forwards them to the native app, which renders them and sends the user's answer back
 * (see {@link DialogProtocol}). Without a host, showing UI is reported as unsupported.
 */
public abstract class UiHost {
    private static volatile UiHost current;

    public static void install(UiHost host) {
        current = host;
    }

    public static UiHost get(String api) {
        UiHost h = current;
        if (h == null) throw Unsupported.of(api + " (no UI host attached)");
        return h;
    }

    /** A dialog became visible. Called on the thread that called show(), as on Android. */
    public abstract void shown(android.app.Dialog dialog);

    public abstract void dismissed(android.app.Dialog dialog);

    public abstract void toast(CharSequence text, int duration);

    /** A plugin asked to open a web page (Context.startActivity with ACTION_VIEW of an http(s) link). */
    public void openUrl(String url) {
        throw Unsupported.of("Context.startActivity (this UI host cannot show links)");
    }
}
