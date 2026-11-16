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
 * onCreate(null), onCreateView(inflater, null, null), onViewCreated(view, null), shows the view in a
 * BottomSheetDialog (a framework Dialog, which reaches the native app through the dialog protocol),
 * then onStart(). When that dialog closes: onDismiss, onStop(), onDestroyView(), onDestroy(); Kartoons
 * (issue #38) stops polling its login there. Nothing is attached to the fragment manager: there are no
 * transactions, back stack, child fragments, saved state or lifecycle observers (getActivity() and
 * getView() stay null; getContext() and requireContext() return the host activity).
 *
 * Call it on the main looper (the runtime opens plugin settings there): the sheet's listeners, and the
 * Handler and Dispatchers.Main work it starts, run on that thread too.
 */
public class BottomSheetDialogFragment extends DialogFragment {
    private Context host;
    private BottomSheetDialog sheet;

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
        BottomSheetDialog dialog = new BottomSheetDialog(host);
        sheet = dialog;
        dialog.setCancelable(isCancelable());
        dialog.setOnCancelListener(this::onCancel);
        dialog.setOnDismissListener(d -> {
            onDismiss(d);
            // Android tears the fragment down after its dialog goes: stop, then its view, then itself.
            onStop();
            onDestroyView();
            onDestroy();
        });
        if (view != null) {
            dialog.setContentView(view);
            onViewCreated(view, null);
        }
        dialog.show();
        if (dialog.isShowing()) onStart();
    }

    /** The real one restores child fragments through the fragment manager; nothing to restore here. */
    @Override
    public void onCreate(Bundle savedInstanceState) {}

    @Override
    public void onViewCreated(View view, Bundle savedInstanceState) {}

    /**
     * The lifecycle callbacks a subclass calls super on. The APK's own (Fragment/DialogFragment) expect a
     * fragment manager and a window, so they are not called.
     */
    @Override
    public void onStart() {}

    @Override
    public void onStop() {}

    @Override
    public void onDestroyView() {}

    @Override
    public void onDestroy() {}

    /** Material's own creates a BottomSheetDialog; this one is shown by show(). */
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        return new BottomSheetDialog(host);
    }

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
