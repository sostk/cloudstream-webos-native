// SPDX-License-Identifier: GPL-3.0-or-later
// No window system exists in the headless runtime. Activities are created only by
// org.csweb.compat.HeadlessActivities (for plugins that require an Activity-typed context); the
// constructor itself fails so nothing can pretend to open a real screen.
package android.app;

public class Activity extends android.view.ContextThemeWrapper {
    public Activity() {
        throw org.csweb.compat.Unsupported.of("android.app.Activity");
    }

    public boolean isFinishing() {
        return true;
    }

    public boolean isDestroyed() {
        return true;
    }

    public void finish() {}

    /** The UI thread is the runtime's main looper (android.os.Looper): now if on it, else posted there. */
    public void runOnUiThread(Runnable action) {
        android.os.Looper main = android.os.Looper.getMainLooper();
        if (main.isCurrentThread()) action.run();
        else new android.os.Handler(main).post(action);
    }

    public android.content.Intent getIntent() {
        return new android.content.Intent();
    }
}
