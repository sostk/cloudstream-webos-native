// SPDX-License-Identifier: GPL-3.0-or-later
package androidx.cardview.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.FrameLayout;

/**
 * AndroidX's CardView: a FrameLayout with rounded corners and elevation. Only the container matters to
 * the dialog protocol (the app draws its own panels), so corner, elevation and colour settings are kept
 * as no-ops. CloudStream's APK has the real class, which draws; this one comes first on the class path.
 */
public class CardView extends FrameLayout {
    public CardView(Context context) {
        super(context);
    }

    public CardView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public CardView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setRadius(float radius) {}

    public float getRadius() {
        return 0;
    }

    public void setCardElevation(float elevation) {}

    public float getCardElevation() {
        return 0;
    }

    public void setMaxCardElevation(float elevation) {}

    public void setCardBackgroundColor(int color) {}

    public void setUseCompatPadding(boolean useCompatPadding) {}

    public void setPreventCornerOverlap(boolean preventCornerOverlap) {}

    public void setContentPadding(int left, int top, int right, int bottom) {
        setPadding(left, top, right, bottom);
    }
}
