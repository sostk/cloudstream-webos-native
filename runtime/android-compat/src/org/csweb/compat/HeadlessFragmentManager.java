// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat;

import android.app.Activity;

import androidx.fragment.app.FragmentManager;

/**
 * The fragment manager a headless FragmentActivity (HeadlessActivities) hands out from
 * getSupportFragmentManager(). It runs no transactions: there is no window to attach fragments to.
 * It carries its activity so the dialog fragments this runtime can show (the Material bottom sheet,
 * com.google.android.material.bottomsheet.BottomSheetDialogFragment) know their context; lookups find
 * nothing, as for an activity whose fragments were never added.
 */
public final class HeadlessFragmentManager extends FragmentManager {
    private final Activity host;

    public HeadlessFragmentManager(Activity host) {
        this.host = host;
    }

    public Activity host() {
        return host;
    }
}
