// SPDX-License-Identifier: GPL-3.0-or-later
package android.widget;

import android.content.Context;
import android.util.AttributeSet;

public abstract class CompoundButton extends Button implements Checkable {
    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }

    private boolean checked;
    private OnCheckedChangeListener listener;

    public CompoundButton(Context context) {
        super(context);
    }

    public CompoundButton(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public CompoundButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    @Override
    public boolean isChecked() {
        return checked;
    }

    @Override
    public void setChecked(boolean checked) {
        if (this.checked == checked) return;
        this.checked = checked;
        if (listener != null) listener.onCheckedChanged(this, checked);
    }

    @Override
    public void toggle() {
        setChecked(!checked);
    }

    @Override
    public boolean performClick() {
        toggle();
        return super.performClick();
    }

    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        this.listener = listener;
    }
}
