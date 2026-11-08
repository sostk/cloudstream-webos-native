// SPDX-License-Identifier: GPL-3.0-or-later
// An image is kept, never drawn (no graphics stack). The dialog protocol describes a clickable image by
// its content description.
package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.view.View;

public class ImageView extends View {
    private Drawable drawable;

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
    }

    public Drawable getDrawable() {
        return drawable;
    }

    public void setImageResource(int resId) {}

    public void setColorFilter(int color) {}
}
