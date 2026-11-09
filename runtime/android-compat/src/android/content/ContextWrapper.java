// SPDX-License-Identifier: GPL-3.0-or-later
// As android.content.ContextWrapper: every call is delegated to the base context (mBase), which can be
// attached once.
package android.content;

import java.io.File;

public class ContextWrapper extends Context {
    Context mBase;

    public ContextWrapper(Context base) {
        mBase = base;
    }

    protected void attachBaseContext(Context base) {
        if (mBase != null) throw new IllegalStateException("Base context already set");
        mBase = base;
    }

    public Context getBaseContext() {
        return mBase;
    }

    @Override public SharedPreferences getSharedPreferences(String name, int mode) { return mBase.getSharedPreferences(name, mode); }
    @Override public Context getApplicationContext() { return mBase.getApplicationContext(); }
    @Override public String getPackageName() { return mBase.getPackageName(); }
    @Override public File getFilesDir() { return mBase.getFilesDir(); }
    @Override public File getCacheDir() { return mBase.getCacheDir(); }
    @Override public ClassLoader getClassLoader() { return mBase.getClassLoader(); }
    @Override public android.content.res.Resources getResources() { return mBase.getResources(); }
    @Override public android.content.res.AssetManager getAssets() { return mBase.getAssets(); }
    @Override public Object getSystemService(String name) { return mBase.getSystemService(name); }
}
