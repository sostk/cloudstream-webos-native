// SPDX-License-Identifier: GPL-3.0-or-later
package android.content;

/** An activity's package and class, as a value (there are no activities to start on webOS). */
public final class ComponentName {
    private final String pkg;
    private final String cls;

    public ComponentName(String pkg, String cls) {
        this.pkg = pkg;
        this.cls = cls;
    }

    public ComponentName(Context context, String cls) {
        this(context.getPackageName(), cls);
    }

    public String getPackageName() {
        return pkg;
    }

    public String getClassName() {
        return cls;
    }
}
