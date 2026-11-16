// SPDX-License-Identifier: GPL-3.0-or-later
package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

public class TextView extends View {
    private CharSequence text = "";
    private CharSequence hint;
    private int inputType;

    public TextView(Context context) {
        super(context);
    }

    public TextView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public TextView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public CharSequence getText() {
        return text;
    }

    public void setText(CharSequence text) {
        this.text = text == null ? "" : text;
    }

    public final void setText(int resid) {
        setText(getContext().getText(resid));
    }

    public CharSequence getHint() {
        return hint;
    }

    public void setHint(CharSequence hint) {
        this.hint = hint;
    }

    public final void setHint(int resid) {
        setHint(getContext().getText(resid));
    }

    public int getInputType() {
        return inputType;
    }

    public void setInputType(int type) {
        inputType = type;
    }

    public int length() {
        return text.length();
    }

    public void setTextSize(float size) {}

    private boolean allCaps;

    /** Stored only: the app shows the text as given (Material buttons set it false anyway). */
    public void setAllCaps(boolean allCaps) {
        this.allCaps = allCaps;
    }

    public boolean isAllCaps() {
        return allCaps;
    }

    public void setTextColor(int color) {}

    public void setSingleLine(boolean singleLine) {}

    public void setSingleLine() {}

    public void setMaxLines(int maxLines) {}

    public void setGravity(int gravity) {}
}
