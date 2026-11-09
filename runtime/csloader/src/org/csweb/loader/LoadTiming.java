// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.loader;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Where a plugin load spends its time, per stage, on the loading thread: the app logs it with the load
 * (startup work, docs/performance.md). Stages that run twice (a plugin loaded again with an Activity)
 * add up. Costs a clock read per stage.
 */
final class LoadTiming {
    private static final ThreadLocal<LoadTiming> CURRENT = new ThreadLocal<>();

    private final Map<String, Long> ms = new LinkedHashMap<>();
    private long last = System.nanoTime();

    static void begin() {
        CURRENT.set(new LoadTiming());
    }

    /** The time since the previous mark goes to `stage`. No-op outside begin()/end(). */
    static void mark(String stage) {
        LoadTiming t = CURRENT.get();
        if (t == null) return;
        long now = System.nanoTime();
        Long before = t.ms.get(stage);
        t.ms.put(stage, (before == null ? 0 : before) + (now - t.last) / 1_000_000);
        t.last = now;
    }

    /** The stages so far, in milliseconds, and stops timing on this thread. */
    static Map<String, Object> end() {
        LoadTiming t = CURRENT.get();
        CURRENT.remove();
        Map<String, Object> out = new LinkedHashMap<>();
        if (t != null) out.putAll(t.ms);
        return out;
    }
}
