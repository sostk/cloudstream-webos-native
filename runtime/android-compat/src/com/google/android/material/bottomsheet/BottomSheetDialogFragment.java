// SPDX-License-Identifier: GPL-3.0-or-later
package com.google.android.material.bottomsheet;

import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;

import androidx.fragment.app.DialogFragment;
import androidx.fragment.app.FragmentManager;

import org.csweb.compat.HeadlessFragmentManager;
import org.csweb.compat.Unsupported;

/**
 * Material's BottomSheetDialogFragment for plugin settings screens. CloudStream's APK does not contain
 * it (R8 dropped it), and the real one needs a FragmentManager attached to a window. Plugins such as
 * CNCVerse PlayFy (issue #21) subclass it and call show(activity.getSupportFragmentManager(), tag).
 *
 * show() runs the parts of the fragment lifecycle a settings sheet relies on, in Android's order:
 * onCreate(null), onCreateView(inflater, null, null), then onViewCreated(view, null), and shows the
 * view in a framework Dialog, which reaches the native app through the dialog protocol. dismiss()
 * closes that dialog. Nothing is attached to the fragment manager: there are no transactions, back
 * stack, child fragments, saved state or lifecycle observers (getActivity() and getView() stay null;
 * getContext() and requireContext() return the host activity).
 */
public class BottomSheetDialogFragment extends DialogFragment {
    private Context host;
    private Dialog sheet;

    public BottomSheetDialogFragment() {
        super();
    }

    public BottomSheetDialogFragment(int contentLayoutId) {
        super(contentLayoutId);
    }

    @Override
    public Context getContext() {
        return host;
    }

    @Override
    public void show(FragmentManager manager, String tag) {
        if (!(manager instanceof HeadlessFragmentManager)) {
            throw Unsupported.of("BottomSheetDialogFragment.show with a fragment manager that has no headless activity");
        }
        if (sheet != null && sheet.isShowing()) return;
        host = ((HeadlessFragmentManager) manager).host();
        onCreate(null);
        View view = onCreateView(LayoutInflater.from(host), null, null);
        sheet = new Dialog(host);
        sheet.setCancelable(isCancelable());
        sheet.setOnCancelListener(this::onCancel);
        sheet.setOnDismissListener(this::onDismiss);
        if (view != null) {
            sheet.setContentView(view);
            onViewCreated(view, null);
        }
        sheet.show();
    }

    /** The real one restores child fragments through the fragment manager; nothing to restore here. */
    @Override
    public void onCreate(Bundle savedInstanceState) {}

    @Override
    public void onViewCreated(View view, Bundle savedInstanceState) {}

    @Override
    public Dialog getDialog() {
        return sheet;
    }

    @Override
    public void dismiss() {
        if (sheet != null) sheet.dismiss();
    }

    @Override
    public void dismissAllowingStateLoss() {
        dismiss();
    }

    /** DialogFragment's own would remove the fragment from its manager; there is nothing to remove. */
    @Override
    public void onDismiss(DialogInterface dialog) {}

    @Override
    public void onCancel(DialogInterface dialog) {}
}
