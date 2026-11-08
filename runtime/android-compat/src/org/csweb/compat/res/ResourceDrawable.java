// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat.res;

import android.graphics.drawable.Drawable;

/** A plugin's drawable resource, by name only (Resources.getDrawable); never drawn. */
public final class ResourceDrawable extends Drawable {
    private final String name;

    public ResourceDrawable(String name) {
        this.name = name;
    }

    /** "drawable/save_icon". */
    public String name() {
        return name;
    }

    @Override
    public String toString() {
        return "ResourceDrawable(" + name + ")";
    }
}
