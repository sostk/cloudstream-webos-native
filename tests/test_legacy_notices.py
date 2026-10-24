# SPDX-License-Identifier: GPL-3.0-or-later
"""Notice generation must follow the shipped ARMv7 files and exact source identities."""
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("legacy_notices", ROOT / "tools/prepare-legacy-notices.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


class LegacyNoticesTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.project = pathlib.Path(self.directory.name)
        self.rootfs = self.project / "runtime/rootfs/armv7"
        self.dist = self.project / "runtime/dist/runtime-armv7"
        self.db = self.rootfs / "lib/apk/db/installed"
        self.db.parent.mkdir(parents=True)
        self.db.write_text(
            "P:art_standalone\nV:1-r3\no:art_standalone\nc:" + "a" * 40 + "\nL:Apache-2.0\nU:https://example.org/art\n"
            "F:usr/lib/art\nR:libart.so\n\n"
            "P:musl\nV:1-r0\no:musl\nc:" + "b" * 40 + "\nL:MIT\nU:https://example.org/musl\n"
            "F:lib\nR:ld-musl-armhf.so.1\n\n")
        self.dist.mkdir(parents=True)
        self.manifest = self.dist / "MANIFEST.json"
        self.manifest.write_text(json.dumps({"arch": "armv7", "files": [
            {"path": "usr/lib/art/libart.so"}, {"path": "lib/ld-musl-armhf.so.1"}]}))
        self.baseline = self.project / "runtime/licenses"
        self.baseline.mkdir()
        table, _ = TOOL.inventory(self.rootfs, self.dist)
        # Different file counts are expected for the main architecture.
        self.baseline.joinpath("packages.tsv").write_text(table.replace("\t1\n", "\t30\n"))
        for origin in ("art_standalone", "musl"):
            path = self.baseline / "texts" / origin / "LICENSE"
            path.parent.mkdir(parents=True)
            path.write_text("test licence text for " + origin)
        self.project.joinpath("runtime/rootfs-armv7.manifest").write_text("art_standalone-1-r3\nmusl-1-r0\n")
        self.output, self.pending = TOOL.paths(self.project)

    def prepare(self):
        TOOL.prepare(self.project, self.rootfs, self.dist)

    def test_reuses_exact_sources_despite_architecture_file_count_changes(self):
        self.prepare()
        self.assertEqual(len(TOOL.rows((self.output / "packages.tsv").read_text())), 2)
        self.assertEqual((self.pending / "packages.tsv").read_text().count("\n"), 1)
        self.assertEqual((self.output / "texts/musl/LICENSE").read_text(), "test licence text for musl")

    def test_changed_source_commit_requires_fresh_collection(self):
        self.db.write_text(self.db.read_text().replace("b" * 40, "c" * 40))
        self.prepare()
        self.assertIn("musl", TOOL.rows((self.pending / "packages.tsv").read_text()))
        self.assertFalse((self.output / "texts/musl").exists())
        with self.assertRaisesRegex(ValueError, "licence texts not collected for musl"):
            TOOL.finish(self.project, self.rootfs, self.dist)
        text = self.pending / "texts/musl/LICENSE"
        text.parent.mkdir(parents=True)
        text.write_text("fresh source text")
        TOOL.finish(self.project, self.rootfs, self.dist)
        self.assertEqual((self.dist / "licenses/texts/musl/LICENSE").read_text(), "fresh source text")

    def test_finish_embeds_matching_notices_and_preserves_main_licences(self):
        before = {str(p): p.read_bytes() for p in self.baseline.rglob("*") if p.is_file()}
        self.prepare()
        TOOL.finish(self.project, self.rootfs, self.dist)
        text = (self.dist / "licenses/NOTICES.md").read_text()
        self.assertIn("ARMv7 Alpine inputs are not yet pinned", text)
        self.assertIn("ART is rebuilt", text)
        self.assertIn("libcsweb_legacy_math.so", text)
        self.assertNotIn("Copied unchanged from the signed Alpine edge packages pinned", text)
        self.assertEqual((self.dist / "licenses/packages.tsv").read_bytes(), (self.output / "packages.tsv").read_bytes())
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.baseline.rglob("*") if p.is_file()})

    def test_finish_rejects_runtime_change_after_preparation(self):
        self.prepare()
        manifest = json.loads(self.manifest.read_text())
        manifest["files"].pop()
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "runtime changed"):
            TOOL.finish(self.project, self.rootfs, self.dist)

    def test_rejects_wrong_architecture_or_missing_provenance(self):
        manifest = json.loads(self.manifest.read_text())
        manifest["arch"] = "aarch64"
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "armv7 runtime"):
            self.prepare()
        manifest["arch"] = "armv7"
        self.manifest.write_text(json.dumps(manifest))
        self.db.write_text(self.db.read_text().replace("a" * 40, ""))
        with self.assertRaisesRegex(ValueError, "source recipe commit"):
            self.prepare()

    def test_rejects_unsafe_inventory_origin(self):
        with self.assertRaisesRegex(ValueError, "invalid package notice row"):
            TOOL.rows("../escape\t" + "a" * 40 + "\tpkg\t1\tMIT\thttps://example.org\t1\n")

    def test_exports_existing_text_blobs_and_new_notice_contents(self):
        import contextlib
        import io
        self.prepare()
        TOOL.finish(self.project, self.rootfs, self.dist)
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            TOOL.export(self.project)
        entries = json.loads(captured.getvalue().removeprefix("LEGACY_NOTICES_EXPORT "))
        by_path = {entry["path"]: entry for entry in entries}
        item = by_path["runtime/licenses-armv7/texts/musl/LICENSE"]
        data = (self.baseline / "texts/musl/LICENSE").read_bytes()
        self.assertEqual(item["sha"], hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest())
        self.assertIn("content", by_path["runtime/licenses-armv7/NOTICES.md"])
        self.assertIn("runtime/rootfs-armv7.manifest", by_path)
        self.assertFalse(any(path.startswith("runtime/licenses/") for path in by_path))


if __name__ == "__main__":
    unittest.main()
