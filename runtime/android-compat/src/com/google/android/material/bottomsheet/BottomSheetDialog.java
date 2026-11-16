// SPDX-License-Identifier: GPL-3.0-or-later
package com.google.android.material.bottomsheet;

import android.app.Dialog;
import android.content.Context;
import android.view.View;

/**
 * Material's BottomSheetDialog, as the dialog a BottomSheetDialogFragment shows. A framework Dialog for
 * the dialog protocol: the native app lays it out for a TV, so the behaviour's state, peek height and
 * dragging are kept but change nothing. Kartoons (issue #38) expands it in onStart:
 * {@code (dialog as BottomSheetDialog).behavior.state = STATE_EXPANDED}.
 */
public class BottomSheetDialog extends Dialog {
    private final BottomSheetBehavior<View> behavior = new BottomSheetBehavior<>();
    private boolean dismissWithAnimation;

    public BottomSheetDialog(Context context) {
        super(context);
    }

    public BottomSheetDialog(Context context, int theme) {
        super(context, theme);
    }

    public BottomSheetBehavior<View> getBehavior() {
        return behavior;
    }

    public void setDismissWithAnimation(boolean dismissWithAnimation) {
        this.dismissWithAnimation = dismissWithAnimation;
    }

    public boolean getDismissWithAnimation() {
        return dismissWithAnimation;
    }

    public boolean getEdgeToEdgeEnabled() {
        return false;
    }
}
