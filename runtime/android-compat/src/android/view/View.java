// SPDX-License-Identifier: GPL-3.0-or-later
// A view-tree node without drawing, measuring or input: plugin settings screens are described to the
// native app (org.csweb.compat.ui.DialogProtocol), which renders them. Only state is kept.
package android.view;

import android.content.Context;
import android.util.AttributeSet;

public class View {
    public static final int NO_ID = -1;
    public static final int VISIBLE = 0x0, INVISIBLE = 0x4, GONE = 0x8;

    public interface OnClickListener {
        void onClick(View v);
    }

    /** Types only (implemented by AndroidX Fragment and its view factory); never called headless. */
    public interface OnCreateContextMenuListener {
        void onCreateContextMenu(ContextMenu menu, View v, ContextMenu.ContextMenuInfo menuInfo);
    }

    public interface OnAttachStateChangeListener {
        void onViewAttachedToWindow(View v);

        void onViewDetachedFromWindow(View v);
    }

    private final Context context;
    private int id = NO_ID;
    private Object tag;
    private int visibility = VISIBLE;
    private boolean enabled = true;
    private ViewParent parent;
    private ViewGroup.LayoutParams layoutParams;
    private OnClickListener onClick;
    private int paddingLeft, paddingTop, paddingRight, paddingBottom;
    private android.graphics.drawable.Drawable background;
    private CharSequence contentDescription;
    private boolean clickable, focusable;
    private float alpha = 1f;
    private static final java.util.concurrent.atomic.AtomicInteger NEXT_ID = new java.util.concurrent.atomic.AtomicInteger(1);

    /** View.generateViewId: ids from 1 that never collide with aapt's (0x7f......) resource ids. */
    public static int generateViewId() {
        for (;;) {
            int result = NEXT_ID.get();
            int next = result + 1;
            if (next > 0x00FFFFFF) next = 1;
            if (NEXT_ID.compareAndSet(result, next)) return result;
        }
    }

    public View(Context context) {
        this.context = context;
    }

    public View(Context context, AttributeSet attrs) {
        this(context);
    }

    public View(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public final Context getContext() {
        return context;
    }

    public int getId() {
        return id;
    }

    public void setId(int id) {
        this.id = id;
    }

    public Object getTag() {
        return tag;
    }

    public void setTag(Object tag) {
        this.tag = tag;
    }

    public int getVisibility() {
        return visibility;
    }

    public void setVisibility(int visibility) {
        this.visibility = visibility;
    }

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public final ViewParent getParent() {
        return parent;
    }

    void assignParent(ViewParent parent) {
        this.parent = parent;
    }

    public ViewGroup.LayoutParams getLayoutParams() {
        return layoutParams;
    }

    public void setLayoutParams(ViewGroup.LayoutParams params) {
        if (params == null) throw new NullPointerException("Layout parameters cannot be null");
        layoutParams = params;
    }

    public void setPadding(int left, int top, int right, int bottom) {
        paddingLeft = left;
        paddingTop = top;
        paddingRight = right;
        paddingBottom = bottom;
    }

    public void setPaddingRelative(int start, int top, int end, int bottom) {
        setPadding(start, top, end, bottom);
    }

    public int getPaddingLeft() { return paddingLeft; }
    public int getPaddingTop() { return paddingTop; }
    public int getPaddingRight() { return paddingRight; }
    public int getPaddingBottom() { return paddingBottom; }
    public int getPaddingStart() { return paddingLeft; }
    public int getPaddingEnd() { return paddingRight; }

    /** Kept, never drawn: the native app styles dialogs itself. */
    public void setBackground(android.graphics.drawable.Drawable background) {
        this.background = background;
    }

    public android.graphics.drawable.Drawable getBackground() {
        return background;
    }

    public void setBackgroundColor(int color) {}

    public void setBackgroundResource(int resid) {}

    /** The accessibility label; the dialog protocol uses it as the text of image buttons. */
    public void setContentDescription(CharSequence description) {
        contentDescription = description;
    }

    public CharSequence getContentDescription() {
        return contentDescription;
    }

    public void setClickable(boolean clickable) {
        this.clickable = clickable;
    }

    public boolean isClickable() {
        return clickable || onClick != null;
    }

    public void setFocusable(boolean focusable) {
        this.focusable = focusable;
    }

    public boolean isFocusable() {
        return focusable;
    }

    public void setAlpha(float alpha) {
        this.alpha = alpha;
    }

    public float getAlpha() {
        return alpha;
    }

    public void setScaleX(float scale) {}

    public void setScaleY(float scale) {}

    public void setElevation(float elevation) {}

    public void requestLayout() {}

    public void invalidate() {}

    public void setMinimumHeight(int minHeight) {}

    public void setMinimumWidth(int minWidth) {}

    public void setOnClickListener(OnClickListener l) {
        onClick = l;
    }

    public boolean hasOnClickListeners() {
        return onClick != null;
    }

    public boolean performClick() {
        if (onClick == null) return false;
        onClick.onClick(this);
        return true;
    }

    public boolean requestFocus() {
        return false;
    }

    public final <T extends View> T findViewById(int id) {
        if (id == NO_ID) return null;
        return findViewTraversal(id);
    }

    public final <T extends View> T findViewWithTag(Object tag) {
        if (tag == null) return null;
        return findViewWithTagTraversal(tag);
    }

    @SuppressWarnings("unchecked")
    <T extends View> T findViewTraversal(int id) {
        return id == this.id ? (T) this : null;
    }

    @SuppressWarnings("unchecked")
    <T extends View> T findViewWithTagTraversal(Object tag) {
        return tag.equals(this.tag) ? (T) this : null;
    }
}
