// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time signatures only. Never dexed or shipped: CloudStream's APK provides the real AndroidX
// class (checked against its DEX: these methods are public and not final there).
package androidx.fragment.app;

public class Fragment {
    public Fragment() {
        throw new UnsupportedOperationException("compile stub");
    }

    public Fragment(int contentLayoutId) {
        throw new UnsupportedOperationException("compile stub");
    }

    public android.content.Context getContext() {
        throw new UnsupportedOperationException("compile stub");
    }

    public void onCreate(android.os.Bundle savedInstanceState) {}

    public android.view.View onCreateView(android.view.LayoutInflater inflater, android.view.ViewGroup container,
            android.os.Bundle savedInstanceState) {
        throw new UnsupportedOperationException("compile stub");
    }

    public void onViewCreated(android.view.View view, android.os.Bundle savedInstanceState) {}
}
