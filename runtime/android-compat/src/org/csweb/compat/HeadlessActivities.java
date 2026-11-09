// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat;

import android.app.Activity;
import android.content.Context;
import android.content.ContextWrapper;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Activity objects for plugins whose load(Context) insists on an Activity type (CloudStream passes its
 * MainActivity there on Android). The instance is a genuine object of the requested class, e.g.
 * androidx.appcompat.app.AppCompatActivity, created without running its constructors (there is no
 * window, lifecycle or fragment host on webOS), with its base context attached. Context methods work
 * through the base context; the activity's own UI machinery is absent, so anything that tries to show
 * a screen fails, and Activity reports itself as finishing and destroyed so well-behaved UI code skips.
 * The exception is a FragmentActivity's fragment manager (attachFragmentHost), which lets settings
 * screens built as Material bottom sheets be shown through the dialog protocol.
 */
public final class HeadlessActivities {
    private HeadlessActivities() {}

    public static Activity create(Class<?> type, Context base) throws ReflectiveOperationException {
        if (!Activity.class.isAssignableFrom(type)) throw new IllegalArgumentException(type + " is not an Activity");
        Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
        Field f;
        try {
            f = unsafeClass.getDeclaredField("theUnsafe");
        } catch (NoSuchFieldException e) {
            f = unsafeClass.getDeclaredField("THE_ONE");  // Android's libcore name
        }
        f.setAccessible(true);
        Object unsafe = f.get(null);
        Method allocate = unsafeClass.getMethod("allocateInstance", Class.class);
        Activity a = (Activity) allocate.invoke(unsafe, type);
        Field mBase = ContextWrapper.class.getDeclaredField("mBase");
        mBase.setAccessible(true);
        mBase.set(a, base);
        attachFragmentHost(a, unsafe, allocate);
        return a;
    }

    /**
     * For a FragmentActivity (AppCompatActivity included): getSupportFragmentManager() returns
     * mFragments.getSupportFragmentManager(), the host callback's fragment manager. The constructor that
     * builds them did not run, so build the host here (also without its constructor, which needs a
     * Handler on a main Looper) holding a HeadlessFragmentManager. Activities of other types, or an APK
     * whose fragment classes differ, are left as they were.
     */
    private static void attachFragmentHost(Activity a, Object unsafe, Method allocate) {
        ClassLoader loader = a.getClass().getClassLoader();
        try {
            Class<?> fragmentActivity = Class.forName("androidx.fragment.app.FragmentActivity", false, loader);
            if (!fragmentActivity.isInstance(a)) return;
            Class<?> callbacks = Class.forName("androidx.fragment.app.FragmentActivity$HostCallbacks", false, loader);
            Class<?> hostCallback = Class.forName("androidx.fragment.app.FragmentHostCallback", false, loader);
            Class<?> controller = Class.forName("androidx.fragment.app.FragmentController", false, loader);
            Object host = allocate.invoke(unsafe, callbacks);
            set(hostCallback, host, "fragmentManager", new HeadlessFragmentManager(a));
            set(hostCallback, host, "context", a);
            set(hostCallback, host, "activity", a);
            set(callbacks, host, "this$0", a);
            Object fragments = controller.getMethod("createController", hostCallback).invoke(null, host);
            set(fragmentActivity, a, "mFragments", fragments);
        } catch (ReflectiveOperationException | LinkageError e) {
            System.err.println("csweb: no fragment host for " + a.getClass().getName() + ": " + e);
        }
    }

    private static void set(Class<?> owner, Object target, String name, Object value) throws ReflectiveOperationException {
        Field f = owner.getDeclaredField(name);
        f.setAccessible(true);
        f.set(target, value);
    }
}
