// SPDX-License-Identifier: GPL-3.0-or-later
package com.google.android.material.bottomsheet;

import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * Material's BottomSheetBehavior, headless: the settings a plugin gives its sheet (state, peek height,
 * skip-collapsed, draggable, hideable) are kept and read back, and state changes reach the callbacks,
 * but nothing moves. The native app always shows the whole sheet as a TV dialog. CloudStream's APK has
 * the real class, which needs a CoordinatorLayout in a window; this one comes first on the runtime's
 * class path.
 */
public class BottomSheetBehavior<V extends View> {
    public static final int STATE_DRAGGING = 1;
    public static final int STATE_SETTLING = 2;
    public static final int STATE_EXPANDED = 3;
    public static final int STATE_COLLAPSED = 4;
    public static final int STATE_HIDDEN = 5;
    public static final int STATE_HALF_EXPANDED = 6;
    public static final int PEEK_HEIGHT_AUTO = -1;

    public abstract static class BottomSheetCallback {
        public abstract void onStateChanged(View bottomSheet, int newState);

        public abstract void onSlide(View bottomSheet, float slideOffset);
    }

    private final List<BottomSheetCallback> callbacks = new ArrayList<>();
    private int state = STATE_COLLAPSED;
    private int peekHeight = PEEK_HEIGHT_AUTO;
    private boolean hideable;
    private boolean skipCollapsed;
    private boolean draggable = true;
    private boolean fitToContents = true;
    private float halfExpandedRatio = 0.5f;
    private int expandedOffset;
    private int maxWidth = -1;
    private int maxHeight = -1;

    public BottomSheetBehavior() {}

    /** The behaviour of a bottom sheet view; headless, every view gets its own. */
    public static <V extends View> BottomSheetBehavior<V> from(V view) {
        return new BottomSheetBehavior<>();
    }

    public int getState() {
        return state;
    }

    public void setState(int state) {
        if (state != STATE_EXPANDED && state != STATE_COLLAPSED && state != STATE_HIDDEN && state != STATE_HALF_EXPANDED) {
            throw new IllegalArgumentException("STATE_" + (state == STATE_DRAGGING ? "DRAGGING" : "SETTLING")
                    + " should not be set externally.");
        }
        if (state == this.state) return;
        this.state = state;
        for (BottomSheetCallback c : new ArrayList<>(callbacks)) c.onStateChanged(null, state);
    }

    public int getPeekHeight() {
        return peekHeight;
    }

    public void setPeekHeight(int peekHeight) {
        this.peekHeight = peekHeight;
    }

    public void setPeekHeight(int peekHeight, boolean animate) {
        setPeekHeight(peekHeight);
    }

    public boolean isHideable() {
        return hideable;
    }

    public void setHideable(boolean hideable) {
        this.hideable = hideable;
    }

    public boolean getSkipCollapsed() {
        return skipCollapsed;
    }

    public void setSkipCollapsed(boolean skipCollapsed) {
        this.skipCollapsed = skipCollapsed;
    }

    public boolean isDraggable() {
        return draggable;
    }

    public void setDraggable(boolean draggable) {
        this.draggable = draggable;
    }

    public boolean isFitToContents() {
        return fitToContents;
    }

    public void setFitToContents(boolean fitToContents) {
        this.fitToContents = fitToContents;
    }

    public float getHalfExpandedRatio() {
        return halfExpandedRatio;
    }

    public void setHalfExpandedRatio(float ratio) {
        this.halfExpandedRatio = ratio;
    }

    public int getExpandedOffset() {
        return expandedOffset;
    }

    public void setExpandedOffset(int offset) {
        this.expandedOffset = offset;
    }

    public int getMaxWidth() {
        return maxWidth;
    }

    public void setMaxWidth(int maxWidth) {
        this.maxWidth = maxWidth;
    }

    public int getMaxHeight() {
        return maxHeight;
    }

    public void setMaxHeight(int maxHeight) {
        this.maxHeight = maxHeight;
    }

    public void addBottomSheetCallback(BottomSheetCallback callback) {
        if (callback != null && !callbacks.contains(callback)) callbacks.add(callback);
    }

    public void removeBottomSheetCallback(BottomSheetCallback callback) {
        callbacks.remove(callback);
    }

    /** Deprecated in Material: replaces every callback with this one. */
    @Deprecated
    public void setBottomSheetCallback(BottomSheetCallback callback) {
        callbacks.clear();
        if (callback != null) callbacks.add(callback);
    }
}
