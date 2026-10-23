# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual legacy backend against fake LG libraries; no TV or root needed."""
import hashlib
import importlib.util
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
KIT = ROOT / "external/webos-native-app-kit"
FIXTURES = ROOT / "tests/fixtures/acb"
SPEC = importlib.util.spec_from_file_location("legacy_starfish", ROOT / "tools/make-legacy-starfish.py")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60, **kwargs)
    if result.returncode:
        raise AssertionError(f"{command}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


@unittest.skipUnless(shutil.which("g++") and (KIT / "media/src/starfish_backend.cpp").exists(),
                     "C++ compiler and pinned kit required")
class LegacyAcbTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.build = pathlib.Path(cls.directory.name)
        cls.generated = cls.build / "generated"
        GENERATOR.generate(KIT, cls.generated)
        cls.includes = ["-I" + str(path) for path in (
            FIXTURES, ROOT / "apps/csweb-tv/src", cls.generated, KIT / "media/include",
            KIT / "logging/include", KIT / "core/include")]
        cls.common = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread"] + cls.includes
        for name, macro in (("full", None), ("no-optional", "ACB_NO_OPTIONAL"),
                            ("incomplete", "ACB_MISSING_REQUIRED")):
            dest = cls.build / name
            dest.mkdir()
            command = cls.common + ["-shared", "-fPIC", str(FIXTURES / "firmware_acb.cpp"),
                                    "-o", str(dest / "libAcbAPI.so.1")]
            if macro:
                command += ["-D" + macro]
            run(command)
        for name, macro in (("full", None), ("no-media-id", "SF_NO_MEDIA_ID")):
            dest = cls.build / ("sf-" + name)
            dest.mkdir()
            command = cls.common + ["-shared", "-fPIC", str(FIXTURES / "firmware_starfish.cpp"),
                                    "-o", str(dest / "libplayerAPIs.so.1")]
            if macro:
                command += ["-D" + macro]
            run(command)
        for name, macro in (("backend", None), ("no-sdl", "NO_SDL_EXPORTS")):
            command = cls.common + ["-rdynamic", str(FIXTURES / "backend_harness.cpp"),
                                    str(cls.generated / "player/legacy_starfish_backend.cpp"),
                                    str(ROOT / "apps/csweb-tv/src/player/legacy_acb.cpp"),
                                    str(KIT / "media/src/media.cpp"), "-ldl", "-o", str(cls.build / name)]
            if macro:
                command += ["-D" + macro]
            run(command)

    def exercise(self, *, acb="full", sf="full", executable="backend", failure=False, **flags):
        trace = self.build / "trace.txt"
        trace.write_text("")
        env = {key: value for key, value in os.environ.items() if not key.startswith("ACB_TEST_")}
        env["LD_LIBRARY_PATH"] = str(self.build / ("sf-" + sf)) + ":" + str(self.build / acb)
        env["ACB_TEST_TRACE"] = str(trace)
        env.pop("APPID", None)
        for key, value in flags.items():
            env["ACB_TEST_" + key.upper()] = str(value)
        run([str(self.build / executable), "failure" if failure else "success"], env=env)
        return trace.read_text().splitlines()

    def test_old_firmware_binds_metadata_display_and_transport_and_releases_each_load(self):
        lines = self.exercise()
        self.assertEqual(sum(x == "acb.create" for x in lines), 2)
        self.assertEqual(sum(x.startswith("acb.destroy") for x in lines), 2)
        self.assertEqual(sum(x == "sf.dtor" for x in lines), 2)
        self.assertIn('acb.video {"video":{"width":1280,"height":720}}', lines)
        self.assertIn('acb.audio {"audio":{"channels":2}}', lines)
        self.assertIn("acb.custom 0,0 1280x720 -> 100,50 960x540 full=0", lines)
        self.assertNotIn("STALE", "\n".join(lines))
        for state in (0, 1, 2, 3):
            self.assertIn("acb.state " + str(state), lines)
        for before, after in (("sf.load.return", "acb.media test-media-id"),
                              ("acb.media test-media-id", "acb.state 1"),
                              ("acb.state 1", 'acb.video {"video":{"width":1280,"height":720}}'),
                              ("sf.unload", "acb.state 0"), ("acb.state 0", "acb.finalize"),
                              ("acb.finalize", "acb.destroy 41")):
            self.assertLess(lines.index(before), lines.index(after))
        self.assertTrue(all('"windowId"' not in x for x in lines if x.startswith("sf.load ")))

    def test_load_callbacks_can_arrive_later_without_losing_metadata(self):
        lines = self.exercise(delayed=1)
        self.assertLess(lines.index("sf.play"), lines.index("acb.media test-media-id"))
        self.assertTrue(any(x.startswith("acb.video ") for x in lines))

    def test_optional_audio_finalize_and_custom_display_are_not_required(self):
        lines = self.exercise(acb="no-optional")
        self.assertIn("acb.display 100,50 960x540 full=0", lines)
        self.assertIn("acb.display 0,0 1920x1080 full=1", lines)
        self.assertFalse(any(x.startswith("acb.audio") for x in lines))

    def test_exported_windows_work_without_acb_or_get_media_id(self):
        lines = self.exercise(acb="absent", sf="no-media-id", exported=1)
        self.assertIn("sdl.place", lines)
        self.assertIn("sdl.destroy", lines)
        self.assertFalse(any(x.startswith("acb.") for x in lines))
        self.assertTrue(all('"windowId":"exported-id"' in x for x in lines if x.startswith("sf.load ")))

    def test_missing_sdl_entrypoints_also_use_acb(self):
        lines = self.exercise(executable="no-sdl")
        self.assertIn("acb.media test-media-id", lines)

    def test_failures_report_errors_and_clean_up(self):
        for options, expected in (({"acb": "absent"}, "libAcbAPI"),
                                   ({"acb": "incomplete"}, "AcbAPI_setMediaVideoData"),
                                   ({"sf": "no-media-id"}, "getMediaID"),
                                   ({"no_app_id": 1}, "missing app id"),
                                   ({"create_fail": 1}, "create/initialize"),
                                   ({"init_fail": 1}, "create/initialize"),
                                   ({"empty_media": 1}, "binding failed"),
                                   ({"bind_fail": 1}, "binding failed"),
                                   ({"load_fail": 1}, "Load refused")):
            with self.subTest(options=options):
                lines = self.exercise(failure=True, **options)
                self.assertIn(expected, next(x for x in lines if x.startswith("expected.error")))
                if "acb.create" in lines and not options.get("create_fail"):
                    self.assertTrue(any(x.startswith("acb.destroy") for x in lines))
                if options.get("init_fail"):
                    self.assertNotIn("sf.load.return", lines)

    def test_no_link_time_acb_dependency(self):
        needed = run(["readelf", "-d", str(self.build / "backend")]).stdout
        self.assertNotIn("libAcbAPI", needed)
        self.assertNotIn("libplayerAPIs", needed)

    def test_compiles_with_old_ndk_metadata_declarations(self):
        # CI's NDK omits audio and declares video with two arguments. The runtime
        # ABI still requires three; the firmware test asserts a real task pointer.
        with tempfile.TemporaryDirectory() as directory:
            include = pathlib.Path(directory)
            dest = include / "appswitching-control-block/AcbAPI.h"
            dest.parent.mkdir()
            header = (FIXTURES / "appswitching-control-block/AcbAPI.h").read_text()
            header = header.replace("int AcbAPI_setMediaAudioData(long, const char *, long *);\n", "")
            header = header.replace("int AcbAPI_setMediaVideoData(long, const char *, long *);",
                                    "int AcbAPI_setMediaVideoData(long, const char *);")
            dest.write_text(header)
            run(self.common[:1] + ["-I" + str(include)] + self.common[1:] +
                ["-fsyntax-only", str(ROOT / "apps/csweb-tv/src/player/legacy_acb.cpp")])

    def test_generation_leaves_kit_untouched_and_refuses_unreviewed_kit_sources(self):
        before = {path: hashlib.sha256((KIT / path).read_bytes()).hexdigest() for path in GENERATOR.PINS}
        GENERATOR.generate(KIT, self.generated)
        self.assertEqual(before, GENERATOR.PINS)
        with tempfile.TemporaryDirectory() as directory:
            kit = pathlib.Path(directory)
            for path in GENERATOR.PINS:
                dest = kit / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(KIT / path, dest)
            (kit / next(iter(GENERATOR.PINS))).write_text("changed")
            with self.assertRaisesRegex(ValueError, "pinned kit source changed"):
                GENERATOR.generate(kit, self.generated)

    @unittest.skipUnless(shutil.which("cmake"), "CMake required for target-isolation integration")
    def test_cmake_builds_separate_backend_only_for_legacy_webos(self):
        with tempfile.TemporaryDirectory() as directory:
            for name, legacy, webos in (("legacy", "ON", "ON"), ("main", "OFF", "ON"),
                                        ("host", "ON", "OFF")):
                root = pathlib.Path(directory) / name
                app = root / "repo/apps/csweb-tv"
                app.mkdir(parents=True)
                for path in GENERATOR.PINS:
                    dest = root / path
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(KIT / path, dest)
                (root / "repo/tools").mkdir()
                shutil.copyfile(ROOT / "tools/make-legacy-starfish.py", root / "repo/tools/make-legacy-starfish.py")
                shutil.copytree(ROOT / "apps/csweb-tv/src/player", app / "src/player")
                shutil.copytree(ROOT / "apps/csweb-tv/cmake", app / "cmake")
                (app / "empty.cpp").write_text("int dummy() { return 0; }\n")
                cmake = (ROOT / "apps/csweb-tv/CMakeLists.txt").read_text()
                start = cmake.index("if (CSWEB_LEGACY AND WNK_WEBOS)\n    include(cmake/LegacyVideoPlane.cmake)")
                hook = cmake[start:cmake.index("endif ()", start) + len("endif ()")]
                (app / "CMakeLists.txt").write_text(
                    'get_filename_component(_csweb_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)\n'
                    'add_library(csweb_player STATIC empty.cpp)\n' + hook)
                include_paths = [KIT / "media/include", KIT / "logging/include", KIT / "core/include", FIXTURES]
                include_args = " ".join('"' + str(x) + '"' for x in include_paths)
                (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.22)
project(LegacyVideoPlane CXX)
set(CMAKE_CXX_STANDARD 17)
option(CSWEB_LEGACY "legacy" OFF)
option(WNK_WEBOS "webos" OFF)
add_library(wnk_media INTERFACE)
add_library(wnk_logging INTERFACE)
target_include_directories(wnk_media INTERFACE ''' + include_args + ''')
add_subdirectory(repo/apps/csweb-tv)
if(CSWEB_LEGACY AND WNK_WEBOS)
  if(NOT TARGET csweb_legacy_starfish)
    message(FATAL_ERROR "legacy backend missing")
  endif()
elseif(TARGET csweb_legacy_starfish)
  message(FATAL_ERROR "legacy backend leaked into main/host build")
endif()
''')
                run(["cmake", "-S", str(root), "-B", str(root / "build"),
                     "-DCSWEB_LEGACY=" + legacy, "-DWNK_WEBOS=" + webos])
                run(["cmake", "--build", str(root / "build")])
                generated = root / "build/repo/apps/csweb-tv/legacy-video/player/legacy_starfish_backend.cpp"
                self.assertEqual(generated.exists(), name == "legacy")


if __name__ == "__main__":
    unittest.main()
