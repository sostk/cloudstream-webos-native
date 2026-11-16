// SPDX-License-Identifier: GPL-3.0-or-later
// An image is kept, never drawn here (no graphics stack). The dialog protocol describes a clickable image
// by its content description, and a bitmap set with setImageBitmap as an image the app draws.
package android.widget;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.view.View;

public class ImageView extends View {
    private Drawable drawable;
    private Bitmap bitmap;

    public ImageView(Context context) {
        super(context);
    }

    public ImageView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ImageView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setImageDrawable(Drawable drawable) {
        this.drawable = drawable;
        this.bitmap = null;
    }

    public Drawable getDrawable() {
        return drawable;
    }

    /** The bitmap is the one image the app is shown (its encoded bytes, see Bitmap); drawables are not. */
    public void setImageBitmap(Bitmap bm) {
        this.bitmap = bm;
        this.drawable = null;
    }

    /** The bitmap last set, for the dialog protocol (not Android API). */
    public Bitmap getImageBitmap() {
        return bitmap;
    }

    public void setImageResource(int resId) {
        this.bitmap = null;
    }

    public void setScaleType(ScaleType type) {}

    public void setAdjustViewBounds(boolean adjust) {}

    public enum ScaleType { MATRIX, FIT_XY, FIT_START, FIT_CENTER, FIT_END, CENTER, CENTER_CROP, CENTER_INSIDE }

    public void setColorFilter(int color) {}
}
