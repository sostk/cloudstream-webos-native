// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time signatures only. Never dexed or shipped: CloudStream's APK provides the real AndroidX
// class (checked against its DEX: these methods are public and not final there).
package androidx.fragment.app;

public class DialogFragment extends Fragment implements android.content.DialogInterface.OnCancelListener,
        android.content.DialogInterface.OnDismissListener {
    public DialogFragment() {
        throw new UnsupportedOperationException("compile stub");
    }

    public DialogFragment(int contentLayoutId) {
        throw new UnsupportedOperationException("compile stub");
    }

    public void show(FragmentManager manager, String tag) {}

    public void dismiss() {}

    public void dismissAllowingStateLoss() {}

    public android.app.Dialog getDialog() {
        throw new UnsupportedOperationException("compile stub");
    }

    public boolean isCancelable() {
        throw new UnsupportedOperationException("compile stub");
    }

    @Override
    public void onCancel(android.content.DialogInterface dialog) {}

    @Override
    public void onDismiss(android.content.DialogInterface dialog) {}
}
