// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.loader;

import com.lagradost.cloudstream3.MainAPIKt;
import com.lagradost.cloudstream3.plugins.BasePlugin;

import org.csweb.compat.MiniJson;

import java.util.Map;

/**
 * Reading a plugin's manifest.json without paying for CloudStream's Jackson mapper first.
 *
 * On the LG C1 the first plugin load spent 1.6 of its 1.9 s in MainAPIKt.getMapper() (the mapper's
 * first use builds Jackson and its Kotlin module), only to read four fields; Home waited for it. The
 * manifest is read here with the runtime's own JSON parser, with the same field types as
 * BasePlugin.Manifest; anything unusual (a version given as a string, a field of another type) falls
 * back to the mapper, so CloudStream's reading still decides those cases. The mapper itself is built on
 * a background thread when the runtime starts (warmUp), where it overlaps with plugin loading instead of
 * delaying it: plugins use it for nearly every response they parse.
 */
final class Manifests {
    private Manifests() {}

    static BasePlugin.Manifest read(String text) throws Exception {
        BasePlugin.Manifest fast = readFast(text);
        return fast != null ? fast : MainAPIKt.getMapper().readValue(text, BasePlugin.Manifest.class);
    }

    /** The manifest when every known field has the type CloudStream declares; null otherwise. */
    static BasePlugin.Manifest readFast(String text) {
        Object decoded;
        try {
            decoded = MiniJson.decode(text);
        } catch (RuntimeException e) {
            return null;
        }
        if (!(decoded instanceof Map)) return null;
        Map<?, ?> fields = (Map<?, ?>) decoded;
        Object name = fields.get("name"), className = fields.get("pluginClassName");
        Object version = fields.get("version"), resources = fields.get("requiresResources");
        if (name != null && !(name instanceof String)) return null;
        if (!(className instanceof String) || ((String) className).isEmpty()) return null;
        if (resources != null && !(resources instanceof Boolean)) return null;
        if (version != null) {
            if (!(version instanceof Integer) && !(version instanceof Long)) return null;
            long n = ((Number) version).longValue();
            if (n < Integer.MIN_VALUE || n > Integer.MAX_VALUE) return null;
        }
        BasePlugin.Manifest m = new BasePlugin.Manifest();
        m.setName((String) name);
        m.setPluginClassName((String) className);
        if (resources != null) m.setRequiresResources((Boolean) resources);
        if (version != null) m.setVersion(((Number) version).intValue());
        return m;
    }

    /**
     * Builds the Jackson mapper on a low-priority daemon thread. 64-bit runtimes only: the legacy ARMv7
     * runtime runs interpreted on slow single-purpose cores and keeps its measured behaviour.
     */
    static void warmUp() {
        String arch = String.valueOf(System.getProperty("os.arch"));
        if (!arch.contains("64")) return;
        Thread t = new Thread(() -> {
            try {
                MainAPIKt.getMapper();
            } catch (Throwable ignored) {
                // The first real use reports any problem with the mapper.
            }
        }, "csweb-warmup");
        t.setDaemon(true);
        t.setPriority(Thread.MIN_PRIORITY);
        t.start();
    }
}
