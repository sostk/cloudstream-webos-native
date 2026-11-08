// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time signature only. Never dexed or shipped: CloudStream's APK provides the real AndroidX
// class (abstract, with a public no-argument constructor and no abstract methods).
package androidx.fragment.app;

public abstract class FragmentManager {
    public FragmentManager() {
        throw new UnsupportedOperationException("compile stub");
    }
}
