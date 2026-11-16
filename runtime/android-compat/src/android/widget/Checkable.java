// SPDX-License-Identifier: GPL-3.0-or-later
package android.widget;

/** A view with a checked state (CompoundButton, Material's checkable buttons). */
public interface Checkable {
    void setChecked(boolean checked);

    boolean isChecked();

    void toggle();
}
