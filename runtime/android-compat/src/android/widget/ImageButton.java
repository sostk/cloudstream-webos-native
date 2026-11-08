// SPDX-License-Identifier: GPL-3.0-or-later
package android.widget;

import android.content.Context;
import android.util.AttributeSet;

public class ImageButton extends ImageView {
    public ImageButton(Context context) {
        super(context);
        setFocusable(true);
    }

    public ImageButton(Context context, AttributeSet attrs) {
        super(context, attrs);
        setFocusable(true);
    }

    public ImageButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        setFocusable(true);
    }
}
