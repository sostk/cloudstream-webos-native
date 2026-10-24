#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate ARMv7 notices from the actual rootfs/dist, never from a guessed package list.

prepare ROOTFS DIST: inventory shipped files, reuse licence texts only for matching
source identities, and list remaining sources under runtime/native/out/legacy-licenses.
Collect that directory's packages.tsv with runtime/collect-licenses.sh, then run
finish ROOTFS DIST to render/check notices and put them into the built runtime.
export: emit Git tree entries for transferring CI-generated notices to a review PR.
"""
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("runtime_notices", ROOT / "runtime/tools/notices.py")
NOTICES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NOTICES)


def rows(text):
    result = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        row = line.split("\t")
        if len(row) != 7 or not re.fullmatch(r"[a-zA-Z0-9_+.-]+", row[0]) or row[0] in (".", ".."):
            raise ValueError("invalid package notice row")
        if not re.fullmatch(r"[0-9a-f]{40}", row[1]):
            raise ValueError("missing source recipe commit for " + row[0])
        if row[0] in result:
            raise ValueError("duplicate origin " + row[0])
        result[row[0]] = row
    if not result:
        raise ValueError("empty package inventory")
    return result


def source_identity(row):
    # Package names and shipped-file counts can differ by CPU architecture.
    return tuple(row[i] for i in (0, 1, 3, 4, 5))


def text_files(directory):
    if not directory.is_dir() or directory.is_symlink():
        return []
    return sorted(p for p in directory.iterdir() if p.is_file() and not p.is_symlink() and p.stat().st_size)


def paths(project):
    return project / "runtime/licenses-armv7", project / "runtime/native/out/legacy-licenses"


def inventory(rootfs, dist):
    manifest = json.loads((dist / "MANIFEST.json").read_text())
    if manifest.get("arch") != "armv7":
        raise ValueError("legacy notices require an armv7 runtime")
    table = NOTICES.table(str(rootfs), str(dist))
    return table, rows(table)


def prepare(project, rootfs, dist):
    table, wanted = inventory(rootfs, dist)
    baseline = project / "runtime/licenses"
    known = rows((baseline / "packages.tsv").read_text())
    output, pending = paths(project)
    # Only generated ARMv7 output/staging is replaced; shared main notices stay read-only.
    for directory in (output, pending):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    (output / "packages.tsv").write_text(table)
    missing = []
    for origin, row in wanted.items():
        files = text_files(baseline / "texts" / origin)
        if origin in known and source_identity(known[origin]) == source_identity(row) and files:
            target = output / "texts" / origin
            target.mkdir(parents=True)
            for file in files:
                shutil.copyfile(file, target / file.name)
        else:
            missing.append(row)
    header = table.splitlines()[0] + "\n"
    (pending / "packages.tsv").write_text(header + "".join("\t".join(row) + "\n" for row in missing))
    print(f"legacy notices: {len(wanted)} origins; {len(missing)} need source collection")


def finish(project, rootfs, dist):
    table, wanted = inventory(rootfs, dist)
    output, pending = paths(project)
    if (output / "packages.tsv").read_text() != table:
        raise ValueError("runtime changed after notice preparation")
    for origin in wanted:
        collected = text_files(pending / "texts" / origin)
        if collected:
            target = output / "texts" / origin
            target.mkdir(parents=True, exist_ok=True)
            for file in collected:
                shutil.copyfile(file, target / file.name)
        if not text_files(output / "texts" / origin):
            raise ValueError("licence texts not collected for " + origin)
    NOTICES.LICENSES, NOTICES.TSV = str(output), str(output / "packages.tsv")
    NOTICES.render()
    notice = output / "NOTICES.md"
    text = notice.read_text()
    old = ("Copied unchanged from the signed Alpine edge packages pinned in `runtime/inputs.lock`. The\n"
           "licence column is Alpine's (SPDX). Source: the package recipe at the aports commit (patches\n"
           "included) and the upstream source it names; licence texts from that source are in `texts/<origin>/`.")
    new = ("This inventory describes the ARMv7 files actually shipped by this runtime, as recorded in\n"
           "`MANIFEST.json` and `packages.tsv`. ARMv7 Alpine inputs are not yet pinned in a lock file.\n"
           "ART is rebuilt from the listed Alpine recipe against wolfSSL 5.9.4, without ART source\n"
           "patches, and signed locally by CI (`runtime/build-art-armv7.sh`). Other Alpine files are\n"
           "copied from signed upstream packages except the rebuilt wolfSSL library described below.\n"
           "The licence column is Alpine's (SPDX). Recipes identify the exact aports source commits;\n"
           "licence texts from those sources are in `texts/<origin>/`.")
    if text.count(old) != 1:
        raise ValueError("shared notice renderer changed; review legacy wording")
    text = text.replace(old, new)
    text = text.replace("| `csweb/csweb-host.dex` |", "| `usr/lib/java/dex/art/natives/libcsweb_legacy_math.so` | "
                        "`runtime/native/legacy_math.c` of this project | GPL-3.0-or-later |\n"
                        "| `csweb/csweb-host.dex` |")
    notice.write_text(text)
    NOTICES.check(str(rootfs), str(dist))
    target = dist / "licenses"
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(output, target)


def export(project):
    """Reuse existing Git blobs when source-identical texts were copied unchanged."""
    output, _ = paths(project)
    entries = []
    for path in sorted(output.rglob("*")):
        if not path.is_file():
            continue
        data = path.read_bytes()
        item = {"path": path.relative_to(project).as_posix(), "mode": "100644", "type": "blob"}
        relative = path.relative_to(output)
        baseline = project / "runtime/licenses" / relative
        if relative.parts[0] == "texts" and baseline.is_file() and baseline.read_bytes() == data:
            item["sha"] = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        else:
            item["content"] = data.decode("utf-8")
        entries.append(item)
    manifest = project / "runtime/rootfs-armv7.manifest"
    entries.append({"path": "runtime/rootfs-armv7.manifest", "mode": "100644", "type": "blob", "content": manifest.read_text()})
    print("LEGACY_NOTICES_EXPORT " + json.dumps(entries, separators=(",", ":")))


if __name__ == "__main__":
    try:
        if len(sys.argv) == 4 and sys.argv[1] in ("prepare", "finish"):
            {"prepare": prepare, "finish": finish}[sys.argv[1]](ROOT, pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]))
        elif sys.argv[1:] == ["export"]:
            export(ROOT)
        else:
            raise ValueError(__doc__)
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
