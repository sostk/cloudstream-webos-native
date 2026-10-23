# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual pinned SDL export function with/without compositor support."""
import importlib.util
import pathlib
import shutil
import signal
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("legacy_sdl", ROOT / "tools/patch-legacy-sdl.py")
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class LegacySdlTest(unittest.TestCase):
    def test_patch_is_idempotent_and_rejects_changed_sources(self):
        source = (ROOT / "tests/fixtures/sdl-exported-window.c").read_text()
        patched = PATCH.patch_text(source)
        self.assertEqual(PATCH.patch_text(patched), patched)
        self.assertEqual(patched.replace(PATCH.GUARD, "", 1), source)
        for bad in ("", source + source, source.replace(PATCH.ANCHOR, "changed")):
            with self.assertRaises(ValueError):
                PATCH.patch_text(bad)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_missing_compositor_protocol_does_not_crash(self):
        fixture = (ROOT / "tests/fixtures/sdl-exported-window.c").read_text()
        original = fixture[fixture.index("const char *WaylandWebOS_CreateExportedWindow"):]
        harness = (ROOT / "tests/fixtures/sdl-exported-window-harness.c").read_text()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for label, function in (("original", original), ("guarded", PATCH.patch_text(original))):
                source = root / (label + ".c")
                source.write_text(harness.replace("/* FUNCTION */", function))
                exe = root / label
                built = subprocess.run(["cc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                                        str(source), "-o", str(exe)], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                if label == "original":
                    # Reproduce the unsafe access with the original upstream function.
                    failed = subprocess.run([str(exe), "missing-table"], capture_output=True, cwd=root)
                    self.assertEqual(failed.returncode, -signal.SIGSEGV)
                else:
                    for case in ("missing-table", "missing-protocol", "missing-both",
                                 "missing-driver", "available"):
                        subprocess.run([str(exe), case], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
