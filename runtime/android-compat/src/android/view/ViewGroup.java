// SPDX-License-Identifier: GPL-3.0-or-later
package android.view;

import android.content.Context;
import android.util.AttributeSet;

import java.util.ArrayList;
import java.util.List;

public abstract class ViewGroup extends View implements ViewParent {
    public static class LayoutParams {
        public static final int MATCH_PARENT = -1;
        @Deprecated public static final int FILL_PARENT = -1;
        public static final int WRAP_CONTENT = -2;
        public int width;
        public int height;

        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }

        public LayoutParams(LayoutParams source) {
            this(source.width, source.height);
        }
    }

    public static class MarginLayoutParams extends LayoutParams {
        public int leftMargin, topMargin, rightMargin, bottomMargin;

        public MarginLayoutParams(int width, int height) {
            super(width, height);
        }

        public MarginLayoutParams(LayoutParams source) {
            super(source);
        }

        public void setMargins(int left, int top, int right, int bottom) {
            leftMargin = left;
            topMargin = top;
            rightMargin = right;
            bottomMargin = bottom;
        }

        public void setMarginStart(int start) {
            leftMargin = start;
        }

        public void setMarginEnd(int end) {
            rightMargin = end;
        }

        public int getMarginStart() {
            return leftMargin;
        }

        public int getMarginEnd() {
            return rightMargin;
        }
    }

    private final List<View> children = new ArrayList<>();

    public ViewGroup(Context context) {
        super(context);
    }

    public ViewGroup(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ViewGroup(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void addView(View child) {
        addView(child, -1);
    }

    public void addView(View child, int index) {
        if (child == null) throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        if (child.getParent() != null) {
            throw new IllegalStateException("The specified child already has a parent. You must call removeView() on the child's parent first.");
        }
        if (index < 0) children.add(child); else children.add(index, child);
        child.assignParent(this);
    }

    public void addView(View child, LayoutParams params) {
        child.setLayoutParams(params);
        addView(child);
    }

    public void addView(View child, int index, LayoutParams params) {
        child.setLayoutParams(params);
        addView(child, index);
    }

    public void addView(View child, int width, int height) {
        addView(child, new LayoutParams(width, height));
    }

    public void removeView(View view) {
        if (children.remove(view)) view.assignParent(null);
    }

    public void removeViewAt(int index) {
        children.remove(index).assignParent(null);
    }

    public void removeAllViews() {
        for (View v : children) v.assignParent(null);
        children.clear();
    }

    public int getChildCount() {
        return children.size();
    }

    public View getChildAt(int index) {
        return index >= 0 && index < children.size() ? children.get(index) : null;
    }

    public int indexOfChild(View child) {
        return children.indexOf(child);
    }

    @Override
    <T extends View> T findViewTraversal(int id) {
        T self = super.findViewTraversal(id);
        if (self != null) return self;
        for (View v : children) {
            T found = v.findViewTraversal(id);
            if (found != null) return found;
        }
        return null;
    }

    @Override
    <T extends View> T findViewWithTagTraversal(Object tag) {
        T self = super.findViewWithTagTraversal(tag);
        if (self != null) return self;
        for (View v : children) {
            T found = v.findViewWithTagTraversal(tag);
            if (found != null) return found;
        }
        return null;
    }
}
