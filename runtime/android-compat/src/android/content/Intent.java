// SPDX-License-Identifier: GPL-3.0-or-later
// android.content.Intent as a plain value object (action, data URI, flags, extras). Context.startActivity
// delivers only ACTION_VIEW of a web link (shown to the viewer by the app); building any intent is harmless.
package android.content;

import android.net.Uri;

import java.util.HashMap;
import java.util.Map;

public class Intent {
    public static final String ACTION_VIEW = "android.intent.action.VIEW";
    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    public static final int FLAG_ACTIVITY_CLEAR_TASK = 0x00008000;
    public static final int FLAG_ACTIVITY_CLEAR_TOP = 0x04000000;

    private String action;
    private Uri data;
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
