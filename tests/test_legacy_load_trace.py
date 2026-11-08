# SPDX-License-Identifier: GPL-3.0-or-later
"""Legacy overlay preserves shared sources and validates its explicit manifest-parser change."""
import importlib.util
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("legacy_trace", ROOT / "tools/instrument-legacy-loader.py")
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)
SOURCE = ROOT / "runtime/csloader/src"
PACKAGE = pathlib.Path("org/csweb/loader")


class LegacyLoadTraceTest(unittest.TestCase):
    def test_overlay_preserves_shared_sources_and_existing_statements(self):
        originals = {p.relative_to(SOURCE): p.read_bytes() for p in SOURCE.rglob("*") if p.is_file()}
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "traced"
            TRACE.instrument(SOURCE, out)
            self.assertEqual(set(originals), {p.relative_to(out) for p in out.rglob("*") if p.is_file()})
            for relative, original in originals.items():
                self.assertEqual((SOURCE / relative).read_bytes(), original)
                modified = (out / relative).read_text()
                if relative not in (PACKAGE / "Main.java", PACKAGE / "Service.java"):
                    self.assertEqual((out / relative).read_bytes(), original)
                    continue
                if relative.name == "Main.java":
                    modified = re.sub(
                        r'\n    // Generated only for the legacy runtime\.[\s\S]*?    // End generated legacy helper\.\n',
                        '', modified, count=1)
                modified = re.sub(r'^.*legacyLoadStage\("[^"]+"\);\n', '', modified, flags=re.MULTILINE)
                modified = modified.replace("        legacyInitializeMath();\n", "")
                modified = modified.replace(
                    "BasePlugin.Manifest manifest = legacyManifest(manifestText);",
                    "BasePlugin.Manifest manifest = MainAPIKt.getMapper().readValue(manifestText, BasePlugin.Manifest.class);")
                self.assertEqual(re.sub(r'\s+', '', modified), re.sub(r'\s+', '', original.decode()))

    def test_stages_are_ordered_and_no_signal_or_stack_dump_is_added(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "traced"
            TRACE.instrument(SOURCE, out)
            main = (out / PACKAGE / "Main.java").read_text()
            service = (out / PACKAGE / "Service.java").read_text()
            # The context comes before the plugin's constructor: a plugin that needs an Activity reads
            # CommonActivity.activity there (docs/runtime/dialog-protocol.md).
            stages = ["classloader", "manifest.open", "manifest.read", "manifest.parse", "class.resolve",
                      "context", "class.initialize_and_construct", "resources", "plugin.load", "settings.inspect"]
            previous = -1
            for stage in stages:
                begin = main.index(f'legacyLoadStage("{stage}.begin")')
                end = main.index(f'legacyLoadStage("{stage}.end")')
                self.assertLess(previous, begin)
                self.assertLess(begin, end)
                previous = end
            nested = [main.index(f'legacyLoadStage("{stage}")') for stage in (
                "manifest.parse.begin", "manifest.minijson.begin", "manifest.minijson.end", "manifest.parse.end")]
            self.assertEqual(nested, sorted(nested))
            self.assertNotIn("MainAPIKt.getMapper()", main)
            for stage in ("request.queued", "request.begin", "request.end", "integrity.begin", "integrity.end",
                          "load.begin", "load.end", "dialogs.begin", "dialogs.end", "providers.begin", "providers.end"):
                self.assertIn(f'legacyLoadStage("{stage}")', service)
            helper = main[main.index("private static final long LEGACY_TRACE_START"):main.index("System.err.flush();")]
            self.assertIn("System.err.println", helper)
            self.assertNotIn("System.out", helper)
            self.assertNotIn("getAllStackTraces", main + service)
            self.assertNotIn("SIGQUIT", main + service)

    def test_existing_output_and_changed_source_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                TRACE.instrument(SOURCE, pathlib.Path(directory))
            with self.assertRaises(ValueError):
                TRACE.instrument(SOURCE, SOURCE)
            source = pathlib.Path(directory) / "source"
            (source / PACKAGE).mkdir(parents=True)
            (source / PACKAGE / "Main.java").write_text("public final class Main {}")
            (source / PACKAGE / "Service.java").write_text("class Service {}")
            out = pathlib.Path(directory) / "invalid"
            with self.assertRaises(ValueError):
                TRACE.instrument(source, out)
            self.assertFalse(out.exists())

    @unittest.skipUnless(shutil.which("java"), "Java is required for manifest schema checks")
    def test_manifest_reader_fields_defaults_and_bad_types(self):
        modules = subprocess.run(["java", "--list-modules"], capture_output=True, text=True, check=True).stdout
        if "jdk.compiler@" not in modules:
            self.skipTest("JDK compiler module unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            out = root / "traced"
            TRACE.instrument(SOURCE, out)
            main = (out / PACKAGE / "Main.java").read_text()
            start = main.index("    // Generated only for the legacy runtime.")
            end = main.index("    // End generated legacy helper.")
            helper = main[start:end]
            # DTO schema fixture, not a replacement for the real-plugin native ARM test.
            stub = root / "BasePlugin.java"
            stub.write_text('''package com.lagradost.cloudstream3.plugins;
public class BasePlugin { public static class Manifest {
    public String name, pluginClassName;
    public boolean requiresResources;
    public Integer version;
    public void setName(String v) { name = v; }
    public void setPluginClassName(String v) { pluginClassName = v; }
    public void setRequiresResources(boolean v) { requiresResources = v; }
    public void setVersion(Integer v) { version = v; }
} }
''')
            test = root / "Main.java"
            test.write_text('''package org.csweb.loader;
import java.util.Map;
import com.lagradost.cloudstream3.plugins.BasePlugin;
import org.csweb.compat.MiniJson;
public final class Main {
''' + helper + r'''
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    static void reject(String value) {
        try { legacyManifest(value); } catch (IllegalArgumentException expected) { return; }
        throw new AssertionError("invalid manifest accepted");
    }
    public static void main(String[] args) {
        BasePlugin.Manifest full = legacyManifest("{\"name\":\"Example\",\"pluginClassName\":\"example.Plugin\",\"version\":27,\"requiresResources\":true,\"future\":123}");
        check(full.name.equals("Example") && full.pluginClassName.equals("example.Plugin"));
        check(full.version == 27 && full.requiresResources);
        check(legacyManifest("{\"pluginClassName\":\"x\",\"version\":2147483647}").version == Integer.MAX_VALUE);
        check(legacyManifest("{\"pluginClassName\":\"x\",\"version\":-2147483648}").version == Integer.MIN_VALUE);
        BasePlugin.Manifest defaults = legacyManifest("{\"pluginClassName\":\"example.Plugin\"}");
        check(defaults.name == null && defaults.version == null && !defaults.requiresResources);
        check(legacyManifest("{\"pluginClassName\":\"x\",\"name\":null,\"version\":null,\"requiresResources\":null}").version == null);
        reject("[]"); reject("{}"); reject("{\"pluginClassName\":123}");
        reject("{\"pluginClassName\":\"\"}"); reject("{\"pluginClassName\":\"x\",\"name\":123}");
        reject("{\"pluginClassName\":\"x\",\"requiresResources\":\"true\"}");
        reject("{\"pluginClassName\":\"x\",\"version\":2147483648}");
        reject("{\"pluginClassName\":\"x\",\"version\":-2147483649}");
        reject("{\"pluginClassName\":\"x\",\"version\":27.0}");
        reject("{\"pluginClassName\":\"x\",\"version\":1.5}");
        reject("{\"pluginClassName\":\"x\",\"version\":\"27\"}");
        reject("{\"pluginClassName\":\"x\"} trailing");
        System.out.println("manifest fields/defaults/type checks pass");
    }
}
''')
            classes = root / "classes"
            classes.mkdir()
            subprocess.run(["java", "-m", "jdk.compiler/com.sun.tools.javac.Main", "-source", "8", "-target", "8",
                            "-d", str(classes), str(stub), str(test),
                            str(ROOT / "runtime/android-compat/src/org/csweb/compat/MiniJson.java")],
                           check=True, capture_output=True, text=True)
            subprocess.run(["java", "-cp", str(classes), "org.csweb.loader.Main"],
                           check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
