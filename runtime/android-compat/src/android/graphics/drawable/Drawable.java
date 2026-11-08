// SPDX-License-Identifier: GPL-3.0-or-later
// Type only: the headless runtime has no graphics stack. Resources.getDrawable returns a named
// placeholder (org.csweb.compat.res.ResourceDrawable) that views keep and nothing draws.
package android.graphics.drawable;

public abstract class Drawable {
}
