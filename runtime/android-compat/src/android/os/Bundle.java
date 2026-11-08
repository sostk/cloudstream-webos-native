// SPDX-License-Identifier: GPL-3.0-or-later
// A string-keyed map of values, the part of android.os.Bundle plugin UI code uses (fragment arguments,
// lifecycle signatures). No parceling: nothing leaves the process. Getters follow Android: a missing
// key or a value of another type gives the default.
package android.os;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public final class Bundle {
    private final Map<String, Object> values = new LinkedHashMap<>();

    public Bundle() {}

    public Bundle(Bundle other) {
        if (other != null) values.putAll(other.values);
    }

    public int size() { return values.size(); }
    public boolean isEmpty() { return values.isEmpty(); }
    public boolean containsKey(String key) { return values.containsKey(key); }
    public Set<String> keySet() { return values.keySet(); }
    public void remove(String key) { values.remove(key); }
    public void clear() { values.clear(); }
    public void putAll(Bundle other) { values.putAll(other.values); }
    public Object get(String key) { return values.get(key); }

    public void putString(String key, String value) { values.put(key, value); }
    public void putInt(String key, int value) { values.put(key, value); }
    public void putLong(String key, long value) { values.put(key, value); }
    public void putBoolean(String key, boolean value) { values.put(key, value); }
    public void putFloat(String key, float value) { values.put(key, value); }
    public void putDouble(String key, double value) { values.put(key, value); }
    public void putBundle(String key, Bundle value) { values.put(key, value); }
    public void putCharSequence(String key, CharSequence value) { values.put(key, value); }

    public String getString(String key) { return get(key, String.class, null); }
    public String getString(String key, String defaultValue) {
        String s = getString(key);
        return s == null ? defaultValue : s;
    }
    public CharSequence getCharSequence(String key) { return get(key, CharSequence.class, null); }
    public Bundle getBundle(String key) { return get(key, Bundle.class, null); }
    public int getInt(String key) { return getInt(key, 0); }
    public int getInt(String key, int defaultValue) { return get(key, Integer.class, defaultValue); }
    public long getLong(String key) { return getLong(key, 0L); }
    public long getLong(String key, long defaultValue) { return get(key, Long.class, defaultValue); }
    public boolean getBoolean(String key) { return getBoolean(key, false); }
    public boolean getBoolean(String key, boolean defaultValue) { return get(key, Boolean.class, defaultValue); }
    public float getFloat(String key) { return getFloat(key, 0f); }
    public float getFloat(String key, float defaultValue) { return get(key, Float.class, defaultValue); }
    public double getDouble(String key) { return getDouble(key, 0d); }
    public double getDouble(String key, double defaultValue) { return get(key, Double.class, defaultValue); }

    private <T> T get(String key, Class<T> type, T defaultValue) {
        Object v = values.get(key);
        return type.isInstance(v) ? type.cast(v) : defaultValue;
    }

    @Override
    public String toString() {
        return "Bundle" + values;
    }
}
