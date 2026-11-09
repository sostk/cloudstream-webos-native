#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the opt-in armv7 loader: progress logs and bounded manifest parsing."""
import pathlib
import shutil
import sys


def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"legacy load trace: expected one occurrence of {old!r}")
    return source.replace(old, new, 1)


def instrument(source, output):
    source, output = pathlib.Path(source), pathlib.Path(output)
    # Never modify the repository's shared sources or accept an existing output tree.
    if source.resolve() == output.resolve() or source.resolve() in output.resolve().parents or output.exists():
        raise ValueError("legacy load trace: output must be a new directory")
    package = pathlib.Path("org/csweb/loader")
    main = (source / package / "Main.java").read_text()
    service = (source / package / "Service.java").read_text()
    helper = '''
    // Generated only for the legacy runtime. Fixed labels only: no plugin data or URLs.
    private static final long LEGACY_TRACE_START = System.nanoTime();
    static void legacyLoadStage(String stage) {
        System.err.println("[legacy-load] elapsed_ms=" +
                ((System.nanoTime() - LEGACY_TRACE_START) / 1000000L) +
                " thread=" + Thread.currentThread().getName() + " stage=" + stage);
        System.err.flush();
    }
    private static BasePlugin.Manifest legacyManifest(String text) {
        Object decoded = MiniJson.decode(text);
        if (!(decoded instanceof Map)) throw new IllegalArgumentException("manifest: not an object");
        Map<?, ?> fields = (Map<?, ?>) decoded;
        BasePlugin.Manifest manifest = new BasePlugin.Manifest();
        Object name = fields.get("name");
        if (name != null && !(name instanceof String))
            throw new IllegalArgumentException("manifest: name must be a string");
        manifest.setName((String) name);
        Object className = fields.get("pluginClassName");
        if (!(className instanceof String) || ((String) className).isEmpty())
            throw new IllegalArgumentException("manifest: pluginClassName must be a nonempty string");
        manifest.setPluginClassName((String) className);
        Object resources = fields.get("requiresResources");
        if (resources != null) {
            if (!(resources instanceof Boolean))
                throw new IllegalArgumentException("manifest: requiresResources must be a boolean");
            manifest.setRequiresResources((Boolean) resources);
        }
        Object version = fields.get("version");
        if (version != null) {
            // MiniJson returns Integer/Long for integer JSON tokens. Keep schema
            // validation in integer arithmetic on the legacy ARM interpreter.
            if (!(version instanceof Integer) && !(version instanceof Long))
                throw new IllegalArgumentException("manifest: version must be an integer");
            long n = ((Number) version).longValue();
            if (n < Integer.MIN_VALUE || n > Integer.MAX_VALUE)
                throw new IllegalArgumentException("manifest: version must be a 32-bit integer");
            manifest.setVersion((int) n);
        }
        return manifest;
    }
    private static void legacyInitializeMath() {
        System.loadLibrary("csweb_legacy_math");
        if (Double.doubleToRawLongBits(Math.rint(27.0)) != 0x403b000000000000L ||
                Double.doubleToRawLongBits(Math.pow(2.0, 4.0)) != 0x4030000000000000L)
            throw new IllegalStateException("legacy Math JNI ABI self-check failed");
        legacyLoadStage("math.bridge.ready");
    }
    // Invoked only by the disposable native CI harness, never by the app.
    public static final class LegacyProbe {
        public static void main(String[] args) {
            int status = 0;
            try {
                String probe = args[0];
                legacyLoadStage("probe." + probe + ".begin");
                if (args.length > 1 && args[1].equals("bridge")) legacyInitializeMath();
                if (probe.equals("math")) {
                    double value = Integer.valueOf(27).doubleValue();
                    System.err.println("[legacy-probe] integer.double.bits=" +
                            Long.toHexString(Double.doubleToRawLongBits(value)));
                    System.err.println("[legacy-probe] finite=" + Double.isFinite(value));
                    System.err.println("[legacy-probe] rint.bits=" +
                            Long.toHexString(Double.doubleToRawLongBits(Math.rint(value))));
                    System.err.println("[legacy-probe] pow.bits=" +
                            Long.toHexString(Double.doubleToRawLongBits(Math.pow(2.0, 4.0))));
                    if (Double.doubleToRawLongBits(value) != 0x403b000000000000L ||
                            !Double.isFinite(value) || Math.rint(value) != value)
                        throw new AssertionError("floating-point probe failed");
                    if (Math.pow(2.0, 4.0) != 16.0 || Math.pow(2.0, -2.0) != 0.25 ||
                            Math.sqrt(4.0) != 2.0 || Math.ceil(-1.5) != -1.0 ||
                            Math.floor(-1.5) != -2.0 || Math.rint(2.5) != 2.0 ||
                            Math.rint(3.5) != 4.0 || Math.sin(0.0) != 0.0 ||
                            Math.atan2(0.0, 1.0) != 0.0 || Math.hypot(3.0, 4.0) != 5.0 ||
                            Math.IEEEremainder(5.0, 2.0) != 1.0 ||
                            !Double.isNaN(Math.sqrt(-1.0)) ||
                            Math.rint(Double.POSITIVE_INFINITY) != Double.POSITIVE_INFINITY ||
                            Double.doubleToRawLongBits(Math.rint(-0.0)) != Long.MIN_VALUE)
                        throw new AssertionError("Math JNI edge-case probe failed");
                } else if (probe.equals("serialization")) {
                    Class<?> type = Class.forName("kotlinx.serialization.json.Json");
                    legacyLoadStage("probe.serialization.class.end");
                    type.getField("Default").get(null);
                } else if (probe.equals("jackson")) {
                    Class<?> type = Class.forName("com.fasterxml.jackson.databind.json.JsonMapper");
                    legacyLoadStage("probe.jackson.class.end");
                    Object builder = type.getMethod("builder").invoke(null);
                    legacyLoadStage("probe.jackson.builder.end");
                    builder.getClass().getMethod("build").invoke(builder);
                } else if (probe.equals("kotlinModule")) {
                    Class<?> type = Class.forName("com.fasterxml.jackson.module.kotlin.KotlinModule$Builder");
                    legacyLoadStage("probe.kotlinModule.class.end");
                    Object builder = type.getDeclaredConstructor().newInstance();
                    legacyLoadStage("probe.kotlinModule.builder.end");
                    type.getMethod("build").invoke(builder);
                } else if (probe.equals("mapper")) {
                    Class<?> type = Class.forName("com.lagradost.cloudstream3.MainAPIKt");
                    legacyLoadStage("probe.mapper.class.end");
                    type.getMethod("getMapper").invoke(null);
                } else throw new IllegalArgumentException("unknown probe");
                legacyLoadStage("probe." + probe + ".end");
            } catch (Throwable failure) {
                failure.printStackTrace(System.err);
                status = 1;
            } finally {
                System.err.flush();
                Runtime.getRuntime().halt(status);
            }
        }
    }
    // End generated legacy helper.
'''
    main = once(main, "public final class Main {", "public final class Main {" + helper)
    main = once(main, "    public static void main(String[] argv) {",
        "    public static void main(String[] argv) {\n        legacyInitializeMath();")

    def before_after(text, statement, stage):
        indent = statement[:len(statement) - len(statement.lstrip())]
        return once(text, statement,
                    f'{indent}legacyLoadStage("{stage}.begin");\n' + statement +
                    f'\n{indent}legacyLoadStage("{stage}.end");')

    main = before_after(main,
        "        PathClassLoader loader = new PathClassLoader(file.getAbsolutePath(), Main.class.getClassLoader());",
        "classloader")
    main = once(main, '        try (InputStream in = loader.getResourceAsStream("manifest.json")) {',
        '        legacyLoadStage("manifest.open.begin");\n' +
        '        try (InputStream in = loader.getResourceAsStream("manifest.json")) {\n' +
        '            legacyLoadStage("manifest.open.end");')
    main = before_after(main,
        "            manifestText = new String(readAll(in, 64 * 1024), StandardCharsets.UTF_8);",
        "manifest.read")
    main = once(main,
        "        BasePlugin.Manifest manifest = Manifests.read(manifestText);",
        '        legacyLoadStage("manifest.parse.begin");\n' +
        '        legacyLoadStage("manifest.minijson.begin");\n' +
        '        BasePlugin.Manifest manifest = legacyManifest(manifestText);\n' +
        '        legacyLoadStage("manifest.minijson.end");\n' +
        '        legacyLoadStage("manifest.parse.end");')
    main = before_after(main,
        "        Class<?> pluginClass = loader.loadClass(manifest.getPluginClassName());", "class.resolve")
    main = before_after(main,
        "        BasePlugin plugin = construct(pluginClass, activity);",
        "class.initialize_and_construct")
    main = before_after(main,
        '        HeadlessContext app = new HeadlessContext(data, "com.lagradost.cloudstream3");', "context")
    main = once(main, "        if (manifest.getRequiresResources()) {",
        '        legacyLoadStage("resources.begin");\n        if (manifest.getRequiresResources()) {')
    main = once(main, "        try {\n            if (plugin instanceof Plugin) {\n                ((Plugin) plugin).load(",
        '        legacyLoadStage("resources.end");\n        legacyLoadStage("plugin.load.begin");\n' +
        "        try {\n            if (plugin instanceof Plugin) {\n                ((Plugin) plugin).load(")
    main = once(main, "        if (plugin instanceof Plugin && ((Plugin) plugin).getOpenSettings() != null) {",
        '        legacyLoadStage("plugin.load.end");\n        legacyLoadStage("settings.inspect.begin");\n' +
        "        if (plugin instanceof Plugin && ((Plugin) plugin).getOpenSettings() != null) {")
    main = once(main, "        return plugin;\n    }",
        '        legacyLoadStage("settings.inspect.end");\n        legacyLoadStage("load.return");\n        return plugin;\n    }')

    def service_stage(text, old, new):
        return once(text, old, new)

    service = service_stage(service, "        String hash = Main.sha256(file);",
        '        Main.legacyLoadStage("integrity.begin");\n        String hash = Main.sha256(file);\n' +
        '        Main.legacyLoadStage("integrity.end");')
    service = service_stage(service, "        BasePlugin plugin = Main.loadPlugin(file, data, hash, null, p.get(\"activity\"));",
        '        Main.legacyLoadStage("load.begin");\n        BasePlugin plugin = Main.loadPlugin(file, data, hash, null, p.get(\"activity\"));\n' +
        '        Main.legacyLoadStage("load.end");')
    service = service_stage(service, '        if (!Main.answerDialogs("load")) return false;',
        '        Main.legacyLoadStage("dialogs.begin");\n        if (!Main.answerDialogs("load")) return false;\n' +
        '        Main.legacyLoadStage("dialogs.end");')
    service = service_stage(service, "        int n = 0;\n        for (MainAPI api : Main.providers()) {",
        '        Main.legacyLoadStage("providers.begin");\n        int n = 0;\n' +
        "        for (MainAPI api : Main.providers()) {")
    service = service_stage(service, '        Main.emit(Main.event("loaded", "plugin", file.getPath(), "providers", n));',
        '        Main.legacyLoadStage("providers.end");\n' +
        '        Main.emit(Main.event("loaded", "plugin", file.getPath(), "providers", n));')
    service = service_stage(service, "            ok = handle(op, p);",
        '            if (op.equals("load")) Main.legacyLoadStage("request.begin");\n            ok = handle(op, p);\n' +
        '            if (op.equals("load")) Main.legacyLoadStage("request.end");')
    # An exclusive load can remain queued behind a stuck earlier request. Record receipt separately.
    service = service_stage(service, "            Running r = new Running();",
        '            if (op.equals("load")) Main.legacyLoadStage("request.queued");\n            Running r = new Running();')
    # Validate all insertion points before copying/writing anything.
    shutil.copytree(source, output)
    (output / package / "Main.java").write_text(main)
    (output / package / "Service.java").write_text(service)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: instrument-legacy-loader.py SOURCE_DIR NEW_OUTPUT_DIR")
    instrument(sys.argv[1], sys.argv[2])
