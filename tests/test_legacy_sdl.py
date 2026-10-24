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
    @unittest.skipUnless(shutil.which("cmake"), "CMake required for build-hook integration")
    def test_external_project_hook_runs_from_root_and_is_legacy_webos_only(self):
        cmake = (ROOT / "apps/csweb-tv/CMakeLists.txt").read_text()
        start = cmake.index("# The kit remains pinned and unmodified.")
        end = cmake.index("# ---- csweb_core", start)
        hook = cmake[start:end]
        with tempfile.TemporaryDirectory() as directory:
            for name, legacy, webos in (("legacy", "ON", "ON"), ("main", "OFF", "ON"),
                                        ("host", "ON", "OFF")):
                root = pathlib.Path(directory) / name
                app = root / "repo/apps/csweb-tv"
                app.mkdir(parents=True)
                (app / "CMakeLists.txt").write_text(
                    'get_filename_component(_csweb_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)\n' + hook)
                (root / "repo/tools").mkdir()
                shutil.copyfile(ROOT / "tools/patch-legacy-sdl.py", root / "repo/tools/patch-legacy-sdl.py")
                sdl = root / "sdl/src/video/wayland"
                sdl.mkdir(parents=True)
                target = sdl / "SDL_waylandwebos_foreign.c"
                shutil.copyfile(ROOT / "tests/fixtures/sdl-exported-window.c", target)
                # This configure command fails if the legacy patch has not run first.
                check = root / "check.cmake"
                check.write_text('file(READ "${SOURCE}" text)\n'
                                 'if(LEGACY AND NOT text MATCHES "CSWeb legacy addition")\n'
                                 'message(FATAL_ERROR "SDL configure ran before the guard")\nendif()\n')
                (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.22)
project(GuardHook NONE)
include(ExternalProject)
option(CSWEB_LEGACY "legacy" OFF)
option(WNK_WEBOS "webos" OFF)
ExternalProject_Add(wnk_sdl_webos_ext
 SOURCE_DIR "${PROJECT_SOURCE_DIR}/sdl"
 DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
 CONFIGURE_COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}/sdl/src/video/wayland/SDL_waylandwebos_foreign.c"
 "-DLEGACY=${CSWEB_LEGACY}" -P "${PROJECT_SOURCE_DIR}/check.cmake"
 BUILD_COMMAND "" INSTALL_COMMAND "")
add_subdirectory(repo/apps/csweb-tv)
'''.replace('"-DLEGACY=${CSWEB_LEGACY}"', '"-DLEGACY=' + ("ON" if name == "legacy" else "OFF") + '"'))
                for command in (["cmake", "-S", str(root), "-B", str(root / "build"),
                                 "-DCSWEB_LEGACY=" + legacy, "-DWNK_WEBOS=" + webos],
                                ["cmake", "--build", str(root / "build"), "--target", "wnk_sdl_webos_ext"]):
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(PATCH.GUARD in target.read_text(), name == "legacy")

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
