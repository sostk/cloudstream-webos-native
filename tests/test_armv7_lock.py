# SPDX-License-Identifier: GPL-3.0-or-later
"""The ARMv7 lock: make_lock.py pins edge packages and the rebuilt ART with its key; the committed lock
covers exactly the manifest and is what make-rootfs.sh can install."""
import hashlib
import importlib.util
import os
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("make_lock", ROOT / "runtime/tools/make_lock.py")
LOCK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LOCK)
MIRROR = "https://example.invalid/releases/download/runtime-inputs-armv7-9"


def entries(text):
    return [line.split() for line in text.splitlines() if line and not line.startswith("#")]


class MakeLockTest(unittest.TestCase):
    def test_rebuilt_art_and_its_key_come_from_the_mirror(self):
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            runtime, art, inputs = d / "runtime", d / "art", d / "inputs"
            runtime.mkdir()
            (art / "keys").mkdir(parents=True)
            (runtime / "rootfs-armv7.manifest").write_text("art_standalone-1-r3\nzlib-1.4-r0\n")
            (art / "art_standalone-1-r3.apk").write_bytes(b"rebuilt")
            (art / "keys" / "k@x-1.rsa.pub").write_bytes(b"key")
            edge = {f"{LOCK.CDN}/main/armv7/zlib-1.4-r0.apk": b"zlib"}
            with mock.patch.object(LOCK, "RUNTIME", str(runtime)), \
                    mock.patch.object(LOCK, "fetch", side_effect=edge.get), \
                    mock.patch.dict(os.environ, {"CSWEB_ARMV7_ART_DIR": str(art)}), \
                    mock.patch("builtins.print"):
                LOCK.main("armv7", MIRROR, str(inputs))
            text = (runtime / "inputs-armv7.lock").read_text()
            self.assertIn(f"# mirror: {MIRROR}\n", text)
            got = {path: (sha, url) for sha, path, url in entries(text)}
            self.assertEqual(got["alpine/armv7/art_standalone-1-r3.apk"],
                             (hashlib.sha256(b"rebuilt").hexdigest(),
                              f"{MIRROR}/alpine_armv7_art_standalone-1-r3.apk"))
            self.assertEqual(got["alpine/armv7/zlib-1.4-r0.apk"][1], f"{LOCK.CDN}/main/armv7/zlib-1.4-r0.apk")
            self.assertEqual(got["alpine/armv7/keys/k@x-1.rsa.pub"][1], f"{MIRROR}/alpine_armv7_keys_k@x-1.rsa.pub")
            self.assertEqual((inputs / "alpine/armv7/keys/k@x-1.rsa.pub").read_bytes(), b"key")

    def test_missing_edge_package_stops(self):
        with tempfile.TemporaryDirectory() as d:
            runtime = pathlib.Path(d)
            (runtime / "rootfs-armv7.manifest").write_text("gone-1-r0\n")
            with mock.patch.object(LOCK, "RUNTIME", str(runtime)), \
                    mock.patch.object(LOCK, "fetch", return_value=None), \
                    mock.patch.dict(os.environ, {"CSWEB_ARMV7_ART_DIR": str(runtime / "none")}), \
                    mock.patch("builtins.print"):
                with self.assertRaises(SystemExit):
                    LOCK.main("armv7", MIRROR, str(runtime / "inputs"))


class CommittedLockTest(unittest.TestCase):
    def test_lock_pins_exactly_the_manifest_and_a_key(self):
        text = (ROOT / "runtime/inputs-armv7.lock").read_text()
        manifest = (ROOT / "runtime/rootfs-armv7.manifest").read_text().split()
        paths = [path for _, path, _ in entries(text)]
        apks = sorted(p[len("alpine/armv7/"):-len(".apk")] for p in paths if p.endswith(".apk"))
        self.assertEqual(apks, sorted(manifest))
        self.assertTrue(any(p.startswith("alpine/armv7/keys/") and p.endswith(".rsa.pub") for p in paths))
        self.assertRegex(text, r"(?m)^# mirror: https://github\.com/.+/releases/download/runtime-inputs-armv7-\d+$")
        for sha, path, url in entries(text):
            self.assertRegex(sha, r"^[0-9a-f]{64}$")
            self.assertTrue(url.startswith("https://"), url)


if __name__ == "__main__":
    unittest.main()
