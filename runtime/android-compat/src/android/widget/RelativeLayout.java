// SPDX-License-Identifier: GPL-3.0-or-later
// RelativeLayout keeps its rules for completeness; the native app lays a dialog out as a column of the
// children in order (the dialog protocol describes structure, not positions).
package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.ViewGroup;

public class RelativeLayout extends ViewGroup {
    public static final int TRUE = -1;
    public static final int LEFT_OF = 0, RIGHT_OF = 1, ABOVE = 2, BELOW = 3, ALIGN_BASELINE = 4, ALIGN_LEFT = 5,
            ALIGN_TOP = 6, ALIGN_RIGHT = 7, ALIGN_BOTTOM = 8, ALIGN_PARENT_LEFT = 9, ALIGN_PARENT_TOP = 10,
            ALIGN_PARENT_RIGHT = 11, ALIGN_PARENT_BOTTOM = 12, CENTER_IN_PARENT = 13, CENTER_HORIZONTAL = 14,
            CENTER_VERTICAL = 15, START_OF = 16, END_OF = 17, ALIGN_START = 18, ALIGN_END = 19,
            ALIGN_PARENT_START = 20, ALIGN_PARENT_END = 21;

    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        private final int[] rules = new int[22];

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public void addRule(int verb) {
            addRule(verb, TRUE);
        }

        public void addRule(int verb, int subject) {
            if (verb >= 0 && verb < rules.length) rules[verb] = subject;
        }

        public void removeRule(int verb) {
            addRule(verb, 0);
        }

        public int getRule(int verb) {
            return verb >= 0 && verb < rules.length ? rules[verb] : 0;
        }

        public int[] getRules() {
            return rules.clone();
        }
    }

    public RelativeLayout(Context context) {
        super(context);
    }

    public RelativeLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public RelativeLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setGravity(int gravity) {}
}
