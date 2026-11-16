// SPDX-License-Identifier: GPL-3.0-or-later
// android.content.Intent as a plain value object (action, data URI, flags, extras). Context.startActivity
// delivers only ACTION_VIEW of a web link (shown to the viewer by the app) and treats a restart of the app's
// task as done; building any intent is harmless.
package android.content;

import android.net.Uri;

import java.util.HashMap;
import java.util.Map;

public class Intent {
    public static final String ACTION_VIEW = "android.intent.action.VIEW";
    public static final String ACTION_MAIN = "android.intent.action.MAIN";
    public static final String CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER";
    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    public static final int FLAG_ACTIVITY_CLEAR_TASK = 0x00008000;
    public static final int FLAG_ACTIVITY_CLEAR_TOP = 0x04000000;

    private String action;
    private Uri data;
    private ComponentName component;
    private int flags;
    private final Map<String, Object> extras = new HashMap<>();

    public Intent() {}

    public Intent(String action) {
        this.action = action;
    }

    public Intent(String action, Uri uri) {
        this.action = action;
        this.data = uri;
    }

    /**
     * The intent that restarts an app's task (a plugin's "restart the app" after saving settings).
     * Context.startActivity treats it as done: the native app loads the plugin again when its
     * settings close, which is what the restart is for.
     */
    public static Intent makeRestartActivityTask(ComponentName mainActivity) {
        Intent i = new Intent(ACTION_MAIN);
        i.component = mainActivity;
        i.flags = FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TASK;
        return i;
    }

    public static Intent makeMainActivity(ComponentName mainActivity) {
        Intent i = new Intent(ACTION_MAIN);
        i.component = mainActivity;
        return i;
    }

    public ComponentName getComponent() {
        return component;
    }

    public Intent setComponent(ComponentName component) {
        this.component = component;
        return this;
    }

    /** A restart of the app's task (makeRestartActivityTask). */
    public boolean isRestartTask() {
        return ACTION_MAIN.equals(action) && (flags & FLAG_ACTIVITY_CLEAR_TASK) != 0;
    }

    public Uri getData() {
        return data;
    }

    public String getDataString() {
        return data == null ? null : data.toString();
    }

    public Intent setData(Uri data) {
        this.data = data;
        return this;
    }

    public int getFlags() {
        return flags;
    }

    public Intent setFlags(int flags) {
        this.flags = flags;
        return this;
    }

    public Intent addFlags(int flags) {
        this.flags |= flags;
        return this;
    }

    public String getAction() {
        return action;
    }

    public Intent setAction(String action) {
        this.action = action;
        return this;
    }

    public Intent putExtra(String name, String value) {
        extras.put(name, value);
        return this;
    }

    public String getStringExtra(String name) {
        Object v = extras.get(name);
        return v instanceof String ? (String) v : null;
    }
}
