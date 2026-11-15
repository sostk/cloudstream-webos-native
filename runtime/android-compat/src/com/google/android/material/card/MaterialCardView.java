// SPDX-License-Identifier: GPL-3.0-or-later
package com.google.android.material.card;

import android.content.Context;
import android.util.AttributeSet;

import androidx.cardview.widget.CardView;

/**
 * Material's MaterialCardView: a CardView with a stroke and a checked state. A container for the dialog
 * protocol, as CardView; Kartoons' settings layout wraps its login controls in one (issue #38). The
 * checked state is kept; stroke and colours are no-ops.
 */
public class MaterialCardView extends CardView {
    private boolean checkable;
    private boolean checked;

    public MaterialCardView(Context context) {
        super(context);
    }

    public MaterialCardView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public MaterialCardView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setStrokeColor(int color) {}

    public void setStrokeWidth(int width) {}

    public int getStrokeWidth() {
        return 0;
    }

    public void setCheckable(boolean checkable) {
        this.checkable = checkable;
    }

    public boolean isCheckable() {
        return checkable;
    }

    public void setChecked(boolean checked) {
        if (checkable) this.checked = checked;
    }

    public boolean isChecked() {
        return checked;
    }

    public void toggle() {
        setChecked(!checked);
    }
}
