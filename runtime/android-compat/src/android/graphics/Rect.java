// SPDX-License-Identifier: GPL-3.0-or-later
package android.graphics;

/** Android's integer rectangle (fields and the common accessors). */
public final class Rect {
    public int left;
    public int top;
    public int right;
    public int bottom;

    public Rect() {}

    public Rect(int left, int top, int right, int bottom) {
        set(left, top, right, bottom);
    }

    public Rect(Rect r) {
        if (r != null) set(r.left, r.top, r.right, r.bottom);
    }

    public void set(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public int width() {
        return right - left;
    }

    public int height() {
        return bottom - top;
    }

    public boolean isEmpty() {
        return left >= right || top >= bottom;
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof Rect)) return false;
        Rect r = (Rect) o;
        return left == r.left && top == r.top && right == r.right && bottom == r.bottom;
    }

    @Override
    public int hashCode() {
        return ((left * 31 + top) * 31 + right) * 31 + bottom;
    }

    @Override
    public String toString() {
        return "Rect(" + left + ", " + top + " - " + right + ", " + bottom + ")";
    }
}
