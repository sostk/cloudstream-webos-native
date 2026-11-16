// SPDX-License-Identifier: GPL-3.0-or-later
// Material's button as a plain framework Button: the app draws every button its own way, so Material's
// shape, ripple, icon and theming are not needed (and need far more of Android than the runtime has;
// CloudStream's own MaterialButton cannot load here). It is checkable, as Material's is, so toggle
// groups of buttons keep their state. CNC Verse's settings build their studio buttons from it.
package com.google.android.material.button;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.Button;
import android.widget.Checkable;

public class MaterialButton extends Button implements Checkable {
    public interface OnCheckedChangeListener {
        void onCheckedChanged(MaterialButton button, boolean isChecked);
    }

    private boolean checkable;
    private boolean checked;
    private OnCheckedChangeListener listener;

    public MaterialButton(Context context) {
        super(context);
    }

    public MaterialButton(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public MaterialButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public boolean isCheckable() {
        return checkable;
    }

    public void setCheckable(boolean checkable) {
        this.checkable = checkable;
    }

    @Override
    public boolean isChecked() {
        return checked;
    }

    /** As Material: only a checkable button changes state. */
    @Override
    public void setChecked(boolean checked) {
        if (!checkable || this.checked == checked) return;
        this.checked = checked;
        if (listener != null) listener.onCheckedChanged(this, checked);
    }

    @Override
    public void toggle() {
        setChecked(!checked);
    }

    @Override
    public boolean performClick() {
        if (checkable) toggle();
        return super.performClick();
    }

    public void addOnCheckedChangeListener(OnCheckedChangeListener listener) {
        this.listener = listener;
    }

    public void removeOnCheckedChangeListener(OnCheckedChangeListener listener) {
        if (this.listener == listener) this.listener = null;
    }

    // Material's styling: the app draws its own buttons.
    public void setCornerRadius(int radius) {}

    public void setStrokeWidth(int width) {}

    public void setIconResource(int id) {}

    public void setIcon(android.graphics.drawable.Drawable icon) {}
}
